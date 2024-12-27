# EDuke32

The [EDuke32] is a open-source implementation engine to [Duke Nuken 3D] from 3D Realms.


![](https://legacy.3drealms.com/duke3d/images/dn3dbox225.gif) 

![](https://legacy.3drealms.com/images/esrb/esrb-m-abgavssc.png)

"*Originally released January 29, 1996.*"

## how to build

Checkout this repository.

```bash
$ git clone https://voidpoint.io/pedro.leao/eduke32 && cd eduke32
```

The variable `RELEASE` is set to default `0`.

```bash
$ make
```

## how to install

```bash
$ make && make install
```

### ubuntu

In [ubuntu] I´m use the [checkinstall] command to build [debian packages].

Is possible use this command to make [rpm] and another linux install formats. 

"*(currently deb, rpm and tgz packages are supported)*".

```bash
# used in Ubuntu 24.04 LTS
$ sudo checkinstall \
        --nodoc \
        --strip \
        --pkggroup games \
        --pkgsource https://voidpoint.io/pedro.leao/eduke32 \
        --pkglicense GPL2 \
        --maintainer "$USER \<$USER@$HOSTNAME\>" \
        --pkgarch $(dpkg --print-architecture) \
        --pkgrelease $(git rev-list --count HEAD) \
        --pkgversion 1.0.0  \
        --pkgname eduke32 \
        --requires 'libgl1 \( \>\= 1.7 \), libsdl2-2 \( \>\= 2.30 \), flac \( \>\= 1.4.3 \), freepats \( \>\= 20060219 \), libvpx9 \( \>\= 1.14 \)' 
```


```bash
checkinstall 1.6.3, Copyright 2010 Felipe Eduardo Sanchez Diaz Duran
           This software is released under the GNU GPL.



*****************************************
**** Debian package creation selected ***
*****************************************

This package will be built according to these values: 

0 -  Maintainer: [ pedroleao <pedroleao@pedro-inspiron7520> ]
1 -  Summary: [ EDuke32 - The open-source implementation of Duke3D engine from 3D-Relms ]
2 -  Name:    [ eduke32 ]
3 -  Version: [ 1.0.0 ]
4 -  Release: [ 10595 ]
5 -  License: [ GPL2 ]
6 -  Group:   [ games ]
7 -  Architecture: [ amd64 ]
8 -  Source location: [ https://voidpoint.io/pedro.leao/eduke32 ]
9 -  Alternate source location: [  ]
10 - Requires: [ libgl1 ( >= 1.7 ), libsdl2-2 ( >= 2.30 ), flac ( >= 1.4.3 ), freepats ( >= 20060219 ), libvpx9 ( >= 1.14 ) ]
11 - Recommends: [  ]
12 - Suggests: [  ]
13 - Provides: [ eduke32 ]
14 - Conflicts: [  ]
15 - Replaces: [  ]
16 - Prerequires: [  ]


Enter a number to change any of them or press ENTER to continue: 

```

### after install

After the install I'm use [eduke32 config] repository to update the config files.

```bash
$ git clone https://github.com/ninjada/eduke32 eduke32_config && cd eduke32_config
```

copy the files

```bash
$ mkdir -p ~/.config/eduke32 && \
cp -r . ~/.config/eduke32 && \
rm -rf ~/.config/eduke32/{EDuke32.app,.git}
```

## Enjoy it!


## Regards

* [Richard Gobeille]
* [ninjada]

[rpm]: https://rpm.org/
[Ubuntu]: ubuntu.com
[eduke32]: https://wiki.eduke32.com/
[ninjada]: https://github.com/ninjada
[Richard Gobeille]: https://voidpoint.io/terminx
[eduke32 config]: https://github.com/ninjada/eduke32
[Duke Nuken 3D]: https://legacy.3drealms.com/duke3d/
[checkinstall]: https://manpages.debian.org/jessie/checkinstall/checkinstall.8.en.html
[debian packages]: https://manpages.debian.org/jessie/checkinstall/checkinstall.8.en.html