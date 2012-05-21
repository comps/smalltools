#!/usr/bin/python

import SocketServer
import os, sys
import socket, struct
import tarfile

from pwd import getpwnam
from grp import getgrnam

from glob import glob
import re


## HELPERS ##

# given a request in the form of a string similar to 'GET /a/b HTTP/1.1',
# returns 3-member tuple - request type (GET), path (/a/b), protocol (HTTP/1.1)
# or None, if the request is malformed
def parse_req(req):
    res = re.match('^([^ ]+) (.+) ([^ ]+)$', req)
    return res.groups()

# given a raw URI from client,
# returns 2-member tuple - "protocol" and sanitized path
# (ie. /tar//a/b would return [0] == "tar", [1] == "/a/b",
#      ///a/b    would return [0] == ''   , [1] == "/a/b"
#      just /a/b would return [0] == None , [1] == "/a/b")
def parse_uri(uri):
    # find protocol
    proto = re.match('/([^/]+)//', uri)
    if proto:
        proto = proto.group(1)
        uri = re.split('/[^/]+/', uri, 1)[1]
    else:
        proto = None

    # sanitize
    uri = os.path.normpath(uri)

    return (proto, uri)

# class able to contain headers, scanned as 'hdrname: hdrvalue'
class headers():
    def __init__(self):
        self.hdrs = []

    def parsefrom(self, data):
        lines = data.split('\n')
        prog = re.compile(': ')
        for i in lines:
            res = prog.split(i, 1)
            if len(res) == 2:
                self.hdrs += [tuple([res[0].strip(), res[1].strip()])]

    def get(self, name):
        for i in self.hdrs:
            if i[0] == name:
                return i[1]
        return None

# UNUSED (but working)
#    def add(self, name, value):
#        if not name or not value:
#            return False
#        self.hdrs += [tuple([str(name).strip(), str(value).strip()])]
#
#    def getallstrings(self):
#        strs = []
#        for i in self.hdrs:
#            strs += [i[0] + ': ' + i[1] + '\r\n']
#        # add headers termination (empty doubleline)
#        strs += ['\r\n\r\n']
#        return strs
#
#    def write(self, f):
#        for i in self.hdrs:
#            f.write(i[0] + ': ' + i[1] + '\r\n')


# the actual request-handling class, instantiated for each client
class req_handler(SocketServer.StreamRequestHandler):

    # send an error message to the client (which will still show up
    # as regular stdout data) and forcibly close the connection using
    # (hopefully) TCP RST packet, resulting in a connection reset error
    def error_close(self, msg=None):
        if msg:
            try:
                self.request.send(msg + '\n')
            except IOError: pass
        self.request.setsockopt(socket.SOL_SOCKET, socket.SO_LINGER,
                                struct.pack('ii', 1, 0))
        self.request.close()

    # error-handling wrapper around wfile.write()
    def send_data(self, data):
        try:
            self.wfile.write(data)
        except IOError:
            self.error_close()
            return False
        return True

    # send a regular file or a directory listing
    def file_send(self):
        path = glob(self.path)
        if not path:
            self.error_close('shell glob empty - no such file or directory')
            return
        elif len(path) > 1:
            msg = ''
            for i in path:
                msg += '\n' + i
            self.error_close('shell glob returned more than one path:' + msg)
            return

        path = path[0]

        if os.path.isdir(path):
            # list directory
            try:
                contents = os.listdir(path)
            except OSError as err:
                self.error_close('error listing directory: ' + err.strerror)
                return

            contents.sort()

            # append '/' to directories
            for i,j in enumerate(contents):
                if os.path.isdir(path + '/' + j):
                    contents[i] += '/'

            # stringify
            contents = '\n'.join(str(n) for n in contents)
            contents += '\n'

            self.send_data(contents)

        else:
            # simply send a regular file
            chunksize = 65536
            try:
                fd = open(path, 'r');
            except IOError as err:
                self.error_close('error sending file: ' + err.strerror)
                return

            try:
                size = os.path.getsize(path)
            except OSError as err:
                self.error_close('error sending file: ' + err.strerror)
                return

            heads = 'HTTP/1.0 200 OK\r\n'
            heads += 'Content-Length: ' + str(size) + '\r\n'
            heads += '\r\n'

            if not self.send_data(heads):
                return

            while 1:
                buff = fd.read(chunksize)
                if not buff:
                    break
                if not self.send_data(buff):
                    break

    # send a tar (compress=''), tgz (compress='gz') or a tbz2 (compress='bz2')
    # made up from the globbed directory listing, added recursively
    def tar_send(self, compress=''):
        paths = glob(self.path)
        if not paths:
            self.error_close('shell glob empty - no such file or directory')
            return

        try:
            tarobj = tarfile.open(mode='w|'+compress, fileobj=self.wfile)
        except IOError:
            self.error_close('error opening tar stream')
            return

        # find common prefix
        prefix = os.path.commonprefix(paths)
        #prefix = os.path.normpath(prefix)
        prefix = os.path.dirname(prefix) + '/'

        for i in paths:
            # substract prefix
            j = i.replace(prefix, '')
            try:
                tarobj.add(name=i, arcname=j)
            except IOError as err:
                self.error_close('tar add error: ' + err.strerror)
                return

        try:
            tarobj.close()
        except IOError: pass

    # main client handler executed by __init__ of this class
    def handle(self):
        readmax = 8192    # filename can be 4096 bytes long

        # receive at most readmax chars
        data = self.request.recv(readmax)
        if not data:
            self.error_close('request too large')
            return

        # chop off and parse the request and the header(s) part
        data = data.split('\n', 1)

        reqtype, path = parse_req(data[0])[:2]
        self.head = headers()
        self.head.parsefrom(data[1])

        # sanity check
        if not reqtype or not path:
            error_close('invalid request')

        # sanitize path, parse out "protocol"
        self.proto, self.path = parse_uri(path)

        # at this point, self.{head|proto|path} should be available,
        # so call request-type handlers
        if reqtype == 'GET':
            self.handle_GET()
        elif reqtype == 'PUT':
            self.handle_PUT()
        else:
            error_close('unsupported request type: ' + reqtype)


    def handle_GET(self):
        if self.proto == 'tar':
            self.tar_send()
        elif self.proto == 'tar.gz' \
          or self.proto == 'targz' \
          or self.proto == 'tgz':
            self.tar_send(compress='gz')
        elif self.proto == 'tar.bz2' \
          or self.proto == 'tarbz2' \
          or self.proto == 'tbz2':
            self.tar_send(compress='bz2')
        elif self.proto == 'file' \
          or self.proto == None:
            self.file_send()
        else:
            self.error_close('unknown protocol')


    def handle_PUT(self):
        chunksize = 65536

        size = self.head.get('Content-Length')
        if not size:
            self.error_close('invalid request')

        size = int(size)

        self.send_data("HTTP/1.1 100 Continue\r\n")

        # na zaklade self.proto se rozhodnout, co s daty delat
        # (zapsat do globnute cesty/souboru, zavolat tar, ..)

#        f = open("recvd", "w")
        while 1:
            if size > chunksize:
                readlen = chunksize
            else:
                readlen = size

            print 'reading', readlen, 'bytes'
#            data = self.request.recv(readlen)
            data = self.rfile.read(readlen)
#            f.write(data)
#            print data

            # zde zapisovat data podle rozhodnuti vyse

            size -= chunksize
            if size <= 0:
                break

        self.send_data("HTTP/1.1 200 OK\r\n")
#        print 'finished!'
#        return


# server class, just to re-define SO_REUSEADDR
# and "kill all threads (do not wait)" option on interrupt (ie. SIGINT)
class usable_tcp_server(SocketServer.ThreadingTCPServer):
    allow_reuse_address = True
    daemon_threads = True


def main():
    user = 'user'
    group = 'user'
    s_addr = ('', 80)

    # gather uid/gid info based on name
    uid = getpwnam(user).pw_uid
    gid = getgrnam(group).gr_gid

    # chroot to server's servedir
    os.chroot(os.getcwd())

    # instantiate the server
    server = usable_tcp_server(s_addr, req_handler)

    # drop privs
    os.setgroups([])
    os.setregid(gid, gid)
    os.setreuid(uid, uid)

    # serve 4ever!
    server.serve_forever()

main()
