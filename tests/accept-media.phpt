--TEST--
PhpRequest::$acceptMedia
--SKIPIF--
<?php if( !extension_loaded('request') ) die('skip '); ?>
--FILE--
<?php
$_SERVER += [
    'HTTP_HOST' => 'example.com',
    'HTTP_ACCEPT' => 'application/xml;q=0.8, application/json;foo=bar, text/*;q=0.2, */*;q=0.1',
];
$request = new PhpRequest();
var_dump($request->acceptMedia);
--EXPECTF--
array(4) {
  [0]=>
  object(stdClass)#%d (3) {
    ["value"]=>
    string(16) "application/json"
    ["quality"]=>
    string(3) "1.0"
    ["params"]=>
    array(1) {
      ["foo"]=>
      string(3) "bar"
    }
  }
  [1]=>
  object(stdClass)#%d (3) {
    ["value"]=>
    string(15) "application/xml"
    ["quality"]=>
    string(3) "0.8"
    ["params"]=>
    array(0) {
    }
  }
  [2]=>
  object(stdClass)#%d (3) {
    ["value"]=>
    string(6) "text/*"
    ["quality"]=>
    string(3) "0.2"
    ["params"]=>
    array(0) {
    }
  }
  [3]=>
  object(stdClass)#%d (3) {
    ["value"]=>
    string(3) "*/*"
    ["quality"]=>
    string(3) "0.1"
    ["params"]=>
    array(0) {
    }
  }
}