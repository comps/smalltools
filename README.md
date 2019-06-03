The smalltools repo
===================

This repository contains miscellaneous smaller tools and utilities that don't
really need their own repositories. To keep commit histories clean, each utility
is kept in its own branch with its own history.

All tools and utilities in any branches of this repository are released under
the license present in the LICENSE file next to this README.md file, unless
specified otherwise in the header of a file.

A quick overview
----------------

* **curlserve** (2012) - A simple HTTP file server that works well with curl(1),
  using raw HTTP and plaintext rather than HTML. Can send directories as
  archives and even has `curl -T` (PUT) upload support.

* **dnscheck** (2016) - A shell script to check a list (file) of DNS hostnames
  against multiple DNS servers and compare their responses to A/AAAA/PTR.
  Useful to verify if your distributed DNS infrastructure has broken updates.

* **ethtoolspeed** (2014) - Simple `LD_PRELOAD`able library to fake speeds
  reported by ethtool(8). For testing purposes.

* **fake_egd** (2015) - Workaround for libvirt to feed qemu-based guests any
  block device (ie. urandom) via virtio-rng. Made for political reasons; the
  maintainer of libvirt in RHEL refused to acknowledge that urandom is secure
  once seeded and libvirt had a hardcoded /dev/urandom check and error. This
  was removed in later versions of libvirt.

* **gitcmds** (2016) - Misc additional git commands and scripts to help my
  workflow.

* **modholder** (2014) - A simple Linux kernel module to increase the refcount
  of another arbitrary module. For testing purposes.

* **null** - This branch! Also a troll for people writing dumb git scripts that
  always presume that remote HEAD is refs/heads/master.

* **relroscan** (2015) - A system scanning bash script to find executables
  potentially vulnerable to some attacks. Checking for stack canaries, RELRO,
  PIE, RPATH/RUNPATH, etc. Originally for Common Criteria testing.

* **reviewer** (2015) - A single-use helper to cleanup a history of one git
  branch across multiple authors. The idea was to preserve the history ordering
  and not cross "author boundaries", but squash/fixup "typo fixes" and other
  small clutter commits. See the script comment.

* **smaeptest** (2014) - A simple Linux kernel module to test if Intel SMEP /
  SMAP is working. Originally for Common Criteria testing.

* **tcp_repeater** (2015/2017) - What it says on the tin; a daemon that listens
  on a port (v1) or a range of ports (v2) and broadcasts any received TCP
  payloads to all connected clients. Originally used for network-based serial
  console for virtual machines where a conserver would connect to the repeater
  and qemu-based guests would too. This mitigated conserver disconnect issues
  when it was connecting directly to qemu and the qemu guest was rebooted.
