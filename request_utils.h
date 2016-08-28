
#ifndef REQUEST_UTILS_H
#define REQUEST_UTILS_H

#include "main/php.h"

PHPAPI zend_bool php_request_is_secure(zval *server);
PHPAPI zend_bool php_request_detect_method(zval *return_value, zval *server);
PHPAPI zend_string *php_request_detect_url(zval *server);
PHPAPI void php_request_normalize_headers(zval *return_value, zval *server);
PHPAPI void php_request_normalize_files(zval *return_value, zval *files);

#endif	/* REQUEST_UTILS_H */

/*
 * Local variables:
 * tab-width: 4
 * c-basic-offset: 4
 * End:
 * vim600: fdm=marker
 * vim: et sw=4 ts=4
 */
