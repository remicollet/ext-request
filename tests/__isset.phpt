--TEST--
PhpRequest::__isset
--SKIPIF--
<?php if( !extension_loaded('request') ) die('skip '); ?>
--FILE--
<?php
$_SERVER['HTTP_HOST'] = 'localhost';
$request = new PhpRequest();
var_dump(isset($request->method));
try {
    isset($request->noSuchProperty);
} catch( Exception $e ) {
    var_dump(get_class($e), $e->getMessage());
}
--EXPECT--
bool(true)
string(16) "RuntimeException"
string(43) "PhpRequest::$noSuchProperty does not exist."