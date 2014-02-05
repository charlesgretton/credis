credis
------

Credis (http://code.google.com/p/credis) is a client library in plain C written by Jonas Romfelt (jonas at romfelt dot se) for communicating with Redis (http://code.google.com/p/redis) servers. Redis is a high performance key-value database, refer to Redis project page for more information.

Modifications
=============

This fork has been modified to remove the majority of the Redis commands and focus purely on the reporting features. This makes is a useful companion with [collectd](http://collectd.org/) since it can be dropped in for use by the official [redis plugin](http://collectd.org/wiki/index.php/Plugin:Redis).

While removing a lot of features, it also improves the compatability range of Redis versions supported, tested up to at least 2.8.4.

Building
========

To build credis-test and credis shared and static libraries simply clone and run:

```
  make
```

To run through a number of credis tests run (presupposed that a Redis server is listening on the default port on localhost):

```
  ./credis-test
```

For use with collectd, you can also now install: (Check `Makefile` to see paths where it will be installed)

```
  make
  make install
```

Authors
=======

All credit is due to the original author [Joans Romfelt](http://code.google.com/p/credis). Modifications made by [Stephen Craton](http://github.com/scraton), a self proclaimed noob at C.
