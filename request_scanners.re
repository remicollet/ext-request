
#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <stdio.h>
#include <string.h>

#include "main/php.h"
#include "Zend/zend_API.h"
#include "Zend/zend_smart_str.h"

/* Adapted from http://re2c.org/examples/example_07.html */

#define YYCTYPE unsigned char

/*!re2c
    re2c:define:YYCURSOR = in->cur;
    re2c:define:YYMARKER = in->mar;
    re2c:define:YYLIMIT = in->lim;
    re2c:yyfill:enable = 0;

    end = "\x00";
    id = [a-zA-Z0-9_\.-]+;
*/

enum scanner_token_type {
    TOKEN_END = 0,
    TOKEN_ERROR,
    TOKEN_UNKNOWN,
    TOKEN_WHITESPACE,
    TOKEN_STRING,
    TOKEN_EQUALS,
    TOKEN_COMMA,
    TOKEN_ID,
    TOKEN_SEMICOLON,
    TOKEN_SLASH
};

struct scanner_input {
    unsigned char * buf;
    unsigned char * tok;
    unsigned char * cur;
    unsigned char * mar;
    unsigned char * lim;
};

struct scanner_token {
    enum scanner_token_type type;
    const unsigned char * yytext;
    size_t yyleng;
};

static zend_string *strip_slashes(const unsigned char *str, size_t len)
{
    register char * pos = str;
    register char * end = str + len;
    smart_str buf = {0};
    smart_str_alloc(&buf, len, 0);
    for(; pos != end; pos++ ) {
        if( *pos != '\\' ) {
            smart_str_appendc_ex(&buf, *pos, 0);
        }
    }
    return buf.s;
}

static inline void token1(struct scanner_token *tok, enum scanner_token_type type,  const unsigned char * yytext, size_t yyleng)
{
    tok->type = type;
    tok->yytext = yytext;
    tok->yyleng = yyleng;
}

static struct scanner_token lex_quoted_str(struct scanner_input *in, unsigned char q)
{
    struct scanner_token tok = {0};
    const unsigned char * start = in->cur;

    unsigned char u = q;
    for (;;) {
        in->tok = in->cur;
        /*!re2c
            *                    { token1(&tok, TOKEN_ERROR, "", 0); return tok; }
            [^\x00]              { u = *in->tok; if (u == q) break; continue; }
            "\\" .               { u = *(in->cur - 1); }
        */
    }
    token1(&tok, TOKEN_STRING, start, in->tok - start);
    return tok;
}

/* {{{ php_request_parse_accept */
/* @see https://www.w3.org/Protocols/rfc2616/rfc2616-sec14.html */
static int php_request_accept_compare(const void *a, const void *b)
{
    Bucket *f = (Bucket *) a;
    Bucket *s = (Bucket *) b;
    zval *first = &f->val;
    zval *second = &s->val;

    if( Z_TYPE_P(first) != IS_OBJECT || Z_TYPE_P(second) != IS_OBJECT ) {
        return 0;
    }

    first = zend_read_property(Z_CE_P(first), first, ZEND_STRL("quality"), 0, NULL);
    second = zend_read_property(Z_CE_P(second), second, ZEND_STRL("quality"), 0, NULL);

    if( !first || !second || Z_TYPE_P(first) != IS_STRING || Z_TYPE_P(second) != IS_STRING ) {
        return 0;
    }

    return -1 * strnatcmp_ex(Z_STRVAL_P(first), Z_STRLEN_P(first), Z_STRVAL_P(second), Z_STRLEN_P(second), 0);
}

static struct scanner_token lex_accept(struct scanner_input *in)
{
    struct scanner_token tok = {0};
    for (;;) {
        in->tok = in->cur;
        /*!re2c
            *   { token1(&tok, TOKEN_UNKNOWN, in->tok, 1); return tok; }
            end { token1(&tok, TOKEN_END, "", 0); return tok; }

            // whitespaces
            [ \t\v\n\r] { continue; tok.type = TOKEN_WHITESPACE; return tok; }

            // character and string literals
            ['"] { tok = lex_quoted_str(in, *(in->cur - 1)); return tok; }
            "''" { token1(&tok, TOKEN_STRING, "", 0); return tok; }

            "=" { token1(&tok, TOKEN_EQUALS, "=", 1); return tok; }
            "/" { token1(&tok, TOKEN_SLASH, "/", 1); return tok; }
            ";" { token1(&tok, TOKEN_SEMICOLON, ";", 1); return tok; }
            "," { token1(&tok, TOKEN_COMMA, ",", 1); return tok; }

            // identifiers
            [a-zA-Z0-9_\./\*-]+ { token1(&tok, TOKEN_ID, in->tok, in->cur - in->tok); return tok; }
        */
    }
}

static int parse_accept_params(struct scanner_input *in, zval *params)
{
    struct scanner_token tok = {0};
    struct scanner_token left;
    struct scanner_token right;
    zend_string *value;

    array_init(params);

    for(;;) {
        // Check for semicolon or comma
        tok = lex_accept(in);
        if( tok.type == TOKEN_SEMICOLON ) {
            // there's another param
        } else if( tok.type == TOKEN_COMMA ) {
            // end of params
            break;
        } else {
            return 0; // err
        }

        // ID
        tok = lex_accept(in);
        if( tok.type != TOKEN_ID ) {
            return 0; // err
        }
        left = tok;

        // Equals
        tok = lex_accept(in);
        if( tok.type != TOKEN_EQUALS ) {
            return 0; // err
        }

        // ID | string
        tok = lex_accept(in);
        if( tok.type != TOKEN_ID && tok.type != TOKEN_STRING ) {
            return 0; // err
        }
        right = tok;

        // Save KV pair
        if( right.type == TOKEN_STRING ) {
            value = strip_slashes(right.yytext, right.yyleng);
        } else {
            value = zend_string_init(right.yytext, right.yyleng, 0);
        }
        add_assoc_str_ex(params, left.yytext, left.yyleng, value);
    }

    return 1;
}

void php_request_parse_accept(zval *return_value, const char *str, size_t len)
{
    struct scanner_input in = {
        str,
        str,
        str,
        0,
        str + len
    };
    struct scanner_token tok = {0};
    zval *qual;
    int con = 1;

    array_init(return_value);

    for(;con;) {
        zval item = {0};
        zval params = {0};
        int ret;

        // MIME type
        tok = lex_accept(&in);
        if( tok.type != TOKEN_ID ) {
            break;
        }

        // Params
        con = parse_accept_params(&in, &params);

        // Save
        array_init(&item);
        add_assoc_stringl_ex(&item, ZEND_STRL("value"), tok.yytext, tok.yyleng);

        // Get quality
        qual = zend_hash_str_find(Z_ARRVAL(params), ZEND_STRL("q"));
        if( qual && Z_TYPE_P(qual) == IS_STRING ) {
            add_assoc_stringl_ex(&item, ZEND_STRL("quality"), Z_STRVAL_P(qual), Z_STRLEN_P(qual));
            zend_hash_str_del(Z_ARRVAL(params), ZEND_STRL("q"));
        } else {
            add_assoc_string_ex(&item, ZEND_STRL("quality"), "1.0");
        }

        add_assoc_zval_ex(&item, ZEND_STRL("params"), &params);
        convert_to_object(&item);
        add_next_index_zval(return_value, &item);
    };

    // Sort
    zend_hash_sort(Z_ARRVAL_P(return_value), php_request_accept_compare, 1);
}
/* }}} php_request_parse_accept */

/* {{{ php_request_parse_content_type */
/* @see https://www.w3.org/Protocols/rfc2616/rfc2616-sec3.html#sec3.7 */
/* @see https://www.w3.org/Protocols/rfc2616/rfc2616-sec14.html */

static struct scanner_token lex_content_type(struct scanner_input *in, const char *str, size_t len)
{
    struct scanner_token tok = {0};
    for (;;) {
        in->tok = in->cur;
        /*!re2c
            *   { token1(&tok, TOKEN_UNKNOWN, in->tok, 1); return tok; }
            end { token1(&tok, TOKEN_END, "", 0); return tok; }

            // whitespaces
            [ \t\v\n\r] { continue; tok.type = TOKEN_WHITESPACE; return tok; }

            // character and string literals
            ['"] { tok = lex_quoted_str(in, *(in->cur - 1)); return tok; }
            "''" { token1(&tok, TOKEN_STRING, "", 0); return tok; }

            "=" { token1(&tok, TOKEN_EQUALS, "=", 1); return tok; }
            "/" { token1(&tok, TOKEN_SLASH, "/", 1); return tok; }
            ";" { token1(&tok, TOKEN_SEMICOLON, ";", 1); return tok; }

            // identifiers
            id { token1(&tok, TOKEN_ID, in->tok, in->cur - in->tok); return tok; }
        */
    }
}

void php_request_parse_content_type(zval *return_value, const char *str, size_t len)
{
    struct scanner_input in = {
        str,
        str,
        str,
        0,
        str + len
    };
    struct scanner_token tok = {0};
    struct scanner_token left;
    struct scanner_token right;
    zend_string *value;
    zval params;
    smart_str buf = {0};


    // Read type
    tok = lex_content_type(&in, str, len);
    if( tok.type != TOKEN_ID ) {
        goto err;
    }
    left = tok;

    // Read slash
    tok = lex_content_type(&in, str, len);
    if( tok.type != TOKEN_SLASH ) {
        goto err;
    }

    // Read subtype
    tok = lex_content_type(&in, str, len);
    if( tok.type != TOKEN_ID ) {
        goto err;
    }
    right = tok;

    // Save
    array_init(return_value);
    smart_str_appendl(&buf, left.yytext, left.yyleng);
    smart_str_appendc(&buf, '/');
    smart_str_appendl(&buf, right.yytext, right.yyleng);
    smart_str_0(&buf);
    add_assoc_str_ex(return_value, ZEND_STRL("value"), buf.s); // might not need to free this
    add_assoc_stringl_ex(return_value, ZEND_STRL("type"), left.yytext, left.yyleng);
    add_assoc_stringl_ex(return_value, ZEND_STRL("subtype"), right.yytext, right.yyleng);

    // Read params
    array_init(&params);
    for(;;) {
        // Read comma
        tok = lex_content_type(&in, str, len);
        if( tok.type != TOKEN_SEMICOLON ) {
            break;
        }

        // Read left
        tok = lex_content_type(&in, str, len);
        if( tok.type != TOKEN_ID ) {
            break;
        }
        left = tok;

        // Read equals
        tok = lex_content_type(&in, str, len);
        if( tok.type != TOKEN_EQUALS ) {
            break;
        }

        // Read right
        tok = lex_content_type(&in, str, len);
        if( tok.type != TOKEN_ID && tok.type != TOKEN_STRING ) {
            break;
        }
        right = tok;

        // Save KV pair
        if( right.type == TOKEN_STRING ) {
            value = strip_slashes(right.yytext, right.yyleng);
        } else {
            value = zend_string_init(right.yytext, right.yyleng, 0);
        }

        if( left.yyleng == sizeof("charset") - 1 && 0 == strncmp(left.yytext, "charset", sizeof("charset") - 1) ) {
            add_assoc_str_ex(return_value, left.yytext, left.yyleng, value);
        } else {
            add_assoc_str_ex(&params, left.yytext, left.yyleng, value);
        }
    }

    add_assoc_zval_ex(return_value, ZEND_STRL("params"), &params);

err:
    return;
}
/* }}} php_request_parse_content_type */

/* {{{ php_request_parse_digest_auth */
/* @see: https://secure.php.net/manual/en/features.http-auth.php */
/* @see: https://www.w3.org/Protocols/rfc2616/rfc2616-sec2.html */
/* @see: https://en.wikipedia.org/wiki/Digest_access_authentication */

static struct scanner_token lex_digest_auth(struct scanner_input *in, const char *str, size_t len)
{
    struct scanner_token tok = {0};

    for (;;) {
        in->tok = in->cur;
        /*!re2c
            *   { token1(&tok, TOKEN_UNKNOWN, in->tok, 1); return tok; }
            end { token1(&tok, TOKEN_END, "", 0); return tok; }

            // whitespaces
            [ \t\v\n\r] { continue; tok.type = TOKEN_WHITESPACE; return tok; }

            // character and string literals
            ['"] { tok = lex_quoted_str(in, *(in->cur - 1)); return tok; }
            "''" { token1(&tok, TOKEN_STRING, "", 0); return tok; }

            "=" { token1(&tok, TOKEN_EQUALS, "=", 1); return tok; }
            "," { token1(&tok, TOKEN_COMMA, ",", 1); return tok; }

            // identifiers
            id { token1(&tok, TOKEN_ID, in->tok, in->cur - in->tok); return tok; }
        */
    }
}

void php_request_parse_digest_auth(zval *return_value, const char *str, size_t len)
{
    struct scanner_input in = {
        str,
        str,
        str,
        0,
        str + len
    };
    struct scanner_token tok = {0};
    struct scanner_token left;
    struct scanner_token right;
    zval need = {0};
    zend_string *value;

    // Build need array
    array_init(&need);
    add_assoc_bool_ex(&need, ZEND_STRL("nonce"), 1);
    add_assoc_bool_ex(&need, ZEND_STRL("nc"), 1);
    add_assoc_bool_ex(&need, ZEND_STRL("cnonce"), 1);
    add_assoc_bool_ex(&need, ZEND_STRL("qop"), 1);
    add_assoc_bool_ex(&need, ZEND_STRL("username"), 1);
    add_assoc_bool_ex(&need, ZEND_STRL("uri"), 1);
    add_assoc_bool_ex(&need, ZEND_STRL("response"), 1);

    // Parse digest auth
    array_init(return_value);
    for(;;) {
        // Read ID
        tok = lex_digest_auth(&in, str, len);
        if( tok.type != TOKEN_ID ) {
            break;
        }
        left = tok;

        // Read equals
        tok = lex_digest_auth(&in, str, len);
        if( tok.type != TOKEN_EQUALS ) {
            break;
        }

        // Read ID | string
        tok = lex_digest_auth(&in, str, len);
        if( tok.type != TOKEN_ID && tok.type != TOKEN_STRING ) {
            break;
        }
        right = tok;

        // Save KV pair
        if( right.type == TOKEN_STRING ) {
            value = strip_slashes(right.yytext, right.yyleng);
        } else {
            value = zend_string_init(right.yytext, right.yyleng, 0);
        }
        add_assoc_str_ex(return_value, left.yytext, left.yyleng, value);
        zend_hash_str_del(Z_ARRVAL_P(&need), left.yytext, left.yyleng);

        // Read comma | end
        tok = lex_digest_auth(&in, str, len);
        if( tok.type != TOKEN_COMMA ) {
            break;
        }
    };

    if( zend_array_count(Z_ARRVAL_P(&need)) > 0 ) {
        // blow result away if invalid
        ZVAL_NULL(return_value);
    } else {
        convert_to_object(return_value);
    }

    zval_dtor(&need);
}
/* }}} php_request_parse_digest_auth */