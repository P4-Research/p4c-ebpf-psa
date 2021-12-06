

##  Troubleshooting problem with libboost-dev.
You may run into trouble with build caused unsupported version of libboost. 

```bash
# to check-out version of boost:	
$ libboost-dev --version 
```	

If you've got too old version of boost libraries, you may want to remove them and install newer releases.

```bash	
# find all libraries 
$ find / -type f -name 'libboost*' 2>/dev/null		
# remove them with:
$ sudo rm -f /path/to/libboost/liboost*
```

# Install boost 1.71.0 or higher from source

For earlier releases of Ubuntu (for example 18.04), you'll need to install from source. We recommend that you use version 1.71. 


```bash	
# download https://www.boost.org/users/history/version_1_71_0.html
$ wget https://www.boost.org/users/history/version_1_71_0.html
# in the directory where you want to put the Boost installation, execute
$ tar --bzip2 -xf /path/to/boost_1_71_0.tar.bz2
$ cd /path/to/boost_1_71_0.tar.bz2
# follow the instructions from README.md
```