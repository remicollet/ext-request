<?php
/**
 *
 * Goals:
 *
 * - Light wrapper around PHP functions (with similar lack of validation)
 * - Buffer for headers and cookies
 * - Helper for HTTP date
 * - Helper for comma- and semicolon-separated header values
 * - Minimalist support for sending a file, and sending json
 * - Self-sendable
 * - Mutable, extendable
 *
 * Capture other auto-sent headers? (E.g. session header, caching headers.)
 *
 */
class PhpResponse
{
    protected $version = '1.1';
    protected $status = 200;
    protected $headers = [];
    protected $cookies = [];
    protected $content;

    public function getVersion() // : string
    {
        return $this->version;
    }

    public function setVersion($version) // : void
    {
        $this->version = $version;
    }

    public function getStatus() // : int
    {
        return $this->status;
    }

    // http_response_code($status)
    public function setStatus($status)
    {
        $this->status = (int) $status;
    }

    public function getHeaders() // : array
    {
        return $this->headers;
    }

    // header("$label: $value", true);
    public function setHeader($label, $value) // : void
    {
        $label = ucwords(strtolower(trim($label)), '-');
        if (! $label) {
            return;
        }

        if (is_array($value)) {
            $value = $this->csv($value);
        }

        $value = trim($value);
        if (! $value) {
            unset($this->headers[$label]);
            return;
        }

        $this->headers[$label] = [$value];
    }

    // header("$label: $value", false);
    public function addHeader($label, $value) // : void
    {
        $label = ucwords(strtolower(trim($label)), '-');
        if (! $label) {
            return;
        }

        if (is_array($value)) {
            $value = $this->csv($value);
        }

        $value = trim($value);
        if (! $value) {
            return;
        }

        $this->headers[$label][] = $value;
    }

    public function getCookies() // : array
    {
        return $this->cookies;
    }

    // setcookie()
    public function setCookie(
        $name,
        $value = "",
        $expire = 0,
        $path = "",
        $domain = "",
        $secure = false,
        $httponly = false
    ) // : void
    {
        $this->cookies[$name] = [
            'raw' => false,
            'value' => $value,
            'expire' => $expire,
            'path' => $path,
            'domain' => $domain,
            'secure' => $secure,
            'httponly' => $httponly,
        ];
    }

    // setrawcookie()
    public function setRawCookie(
        $name,
        $value = "",
        $expire = 0,
        $path = "",
        $domain = "",
        $secure = false,
        $httponly = false
    ) // : void
    {
        $this->cookies[$name] = [
            'raw' => true,
            'value' => $value,
            'expire' => $expire,
            'path' => $path,
            'domain' => $domain,
            'secure' => $secure,
            'httponly' => $httponly,
        ];
    }

    public function getContent() // : mixed
    {
        return $this->content;
    }

    public function setContent($content) // : void
    {
        $this->content = $content;
    }

    public function setContentJson($value, $options = 0, $depth = 512) // : void
    {
        $content = json_encode($value, $options, $depth);
        if ($content === false) {
            throw new RuntimeException(
                "JSON encoding failed: " . json_last_error_msg(),
                json_last_error()
            );
        }
        $this->setContent($content);
        $this->setHeader('Content-Type', 'application/json');
    }

    // cf. https://www.iana.org/assignments/cont-disp/cont-disp.xhtml for
    // $disposition and $params values
    public function setContentResource($fh, $disposition, array $params = []) // : void
    {
        if (! is_resource($fh)) {
            throw new RuntimeException("Content must be a resource.");
        }
        $this->setHeader('Content-Type', 'application/octet-stream');
        $this->setHeader('Content-Transfer-Encoding',  'binary');
        if ($params) {
            $disposition .= ';' . $this->semicsv($params);
        }
        $this->setHeader('Content-Disposition', $disposition);
        $this->setContent($fh);
    }

    public function setDownload($fh, $name, array $params = []) // : void
    {
        $params['filename'] = '"' . rawurlencode($name) . '"';
        $this->setContentResource($fh, 'attachment', $params);
    }

    public function setDownloadInline($fh, $name, array $params = []) // : void
    {
        $params['filename'] = '"' . rawurlencode($name) . '"';
        $this->setContentResource($fh, 'inline', $params);
    }

    /**
     *
     * Converts any recognizable date format to an RFC 1123 date.
     *
     * @param mixed $date The incoming date value.
     *
     * @return string An RFC 1123 formatted date.
     *
     * @see https://www.w3.org/Protocols/rfc2616/rfc2616-sec3.html Section 3.3.1,
     * "servers ... MUST only generate the RFC 1123 format for representing
     * HTTP-date values in header fields."
     */
    public function date($date) // : string
    {
        if ($date instanceof DateTime) {
            $date = clone $date;
        } else {
            $date = new DateTime($date);
        }

        $date->setTimeZone(new DateTimeZone('UTC'));
        return $date->format(DateTime::RFC1123);
    }

    protected function csv(array $values) // : string
    {
        $csv = [];
        foreach ($values as $key => $val) {
            if (is_int($key)) {
                $csv[] = $val;
            } elseif (is_array($val)) {
                $csv[] = $key . ';' . $this->semicsv($val);
            } else {
                $csv[] = "{$key}={$val}";
            }
        }
        return implode(', ', $csv);
    }

    protected function semicsv(array $values) // : string
    {
        $semicsv = [];
        foreach ($values as $key => $val) {
            if (is_int($key)) {
                $semicsv[] = $val;
            } else {
                $semicsv[] = "{$key}={$val}";
            }
        }
        return implode(';', $semicsv);
    }


    // if headers_sent() then fail?
    public function send() // : void
    {
        // if headers_sent() then fail?
        $this->sendStatus();
        $this->sendHeaders();
        $this->sendContent();
    }

    protected function sendStatus() // : void
    {
        header("HTTP/{$this->version} {$this->status}", true, $this->status);
    }

    // capture other cookies at send-time, e.g. session ID?
    protected function sendHeaders() // : void
    {
        foreach ($this->headers as $label => $values) {
            foreach ($values as $value) {
                header("{$label}: {$value}", false);
            }
        }

        foreach ($this->cookies as $name => $args) {
            if ($args['raw']) {
                setrawcookie(
                    $name,
                    $args['value'],
                    $args['expire'],
                    $args['path'],
                    $args['domain'],
                    $args['secure'],
                    $args['httponly']
                );
            } else {
                setcookie(
                    $name,
                    $args['value'],
                    $args['expire'],
                    $args['path'],
                    $args['domain'],
                    $args['secure'],
                    $args['httponly']
                );
            }
        }
    }

    protected function sendContent() // : void
    {
        if (is_resource($this->content)) {
            rewind($this->content);
            fpassthru($this->content);
            return;
        }

        if (is_object($this->content) && is_callable($this->content)) {
            call_user_func($this->content);
            return;
        }

        echo $this->content;
    }
}