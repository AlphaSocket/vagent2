Varnish Agent installation instructions
=======================================

Build requirements
------------------

* Varnish 4.1 The varnishapi development files must be available. E.g:
  apt-get install libvarnishapi-dev
* libmicrohttpd (apt-get install libmicrohttpd-dev)
* libcurl development files
* pkg-config
* Common build environment (C compiler, make, etc).

If you are building from the git repository, not a tar-ball, you will need
automake, autoconf, libcurl, python-docutils for rst2man, and possibly others.

On RHEL6 you need the EPEL package libmicrohttpd-devel.

Build instructions
------------------

Installing from git repo only::

	 $ ./autogen.sh

Common::

	$ ./configure
	$ make
	# make install

Pre-built packages
------------------

Pre-built varnish-agent packages for Debian and RedHat are available
from [Packagecloud](https://packagecloud.io).

[For Varnish 4.1](https://packagecloud.io/varnishcache/varnish41) [[Installation guidelines]](https://packagecloud.io/varnishcache/varnish41/install)

[For Varnish 4.0](https://packagecloud.io/varnishcache/varnish40) [[Installation guidelines]](https://packagecloud.io/varnishcache/varnish40/install)

Previous packages are available in [repo.varnish-cache.org](repo.varnish-cache.org)

See https://www.varnish-cache.org/installation/debian
or https://www.varnish-cache.org/installation/redhat for further details.

Running
-------

See README.rst (or the manual) for what arguments the agent accepts.

Bugs
----

All known bugs are tracked on http://github.com/varnish/vagent2/issues

See the manual page (or README.rst) for some of the more pressing bugs.
