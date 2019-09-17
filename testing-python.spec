# SPEC file overview:
# https://docs.fedoraproject.org/en-US/quick-docs/creating-rpm-packages/#con_rpm-spec-file-overview
# Fedora packaging guidelines:
# https://docs.fedoraproject.org/en-US/packaging-guidelines/

# TODO: sha/md5 check of source
%undefine _disable_source_fetch

# no debuginfo
%global debug_package %{nil}

# for brp-python-bytecompile
# TODO: on Fedora, turn py_auto_byte_compile off
%undefine __os_install_post
# no check-buildroot
%undefine __arch_install_post

%global pythonver 3.7.4

%global pymodlist \
		pexen \
		somethingelse

Name: testing-python
Version: %pythonver
Release: 0%{?dist}
Summary: Standalone python venv for system-independent python programs/tests

License: Python
URL: https://www.python.org/
Source0: https://www.python.org/ftp/python/%{pythonver}/Python-%{pythonver}.tgz

BuildRequires: git bzip2 bzip2-devel findutils gcc gcc-c++ gdbm-devel glibc-devel libffi-devel libuuid-devel openssl-devel pkgconfig sqlite-devel gdb tar xz-devel zlib-devel readline-devel tcl-devel tk-devel
#Requires:
BuildArch: noarch


%description
Woohoo, hurray for mandatory descriptions.


%prep
%setup -q -c


%build
cat > nosync.c <<EOF
void sync(void) { return; }
void syncfs(int fd) { (void)fd; return; }
int fsync(int fd) { (void)fd; return 0; }
int fdatasync(int fd) { (void)fd; return 0; }
EOF
cc -o nosync.so -fPIC -shared -Wall -O2 nosync.c
export LD_PRELOAD="${PWD}/nosync.so"

# pyinstaller needs --enable-shared
#	--with-lto \
#	--enable-optimizations \  # only on x86, takes a while
cd Python-%{pythonver}
./configure \
	--enable-shared \
	--with-ssl-default-suites=openssl \
	--prefix="%{buildroot}"

make %{?_smp_mflags}

# TODO: install modules via pip3
#pkgs="%%{pymodlist}"
#echo .././././python3 -m pip install $pkgs


%install
cd "Python-%{pythonver}"
#%%make_install
#make -C "Python-%{pythonver}" install
make install


%files
%%doc
%license
# TODO
# also exclude (somehow) extraneous files


%changelog

