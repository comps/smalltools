#!/usr/bin/python

import SocketServer
import os, sys
import socket, struct
import tarfile

from glob import glob
import re


# given a request in the form of a string similar to 'GET /a/b HTTP/1.1',
# returns 3-member tuple - request type (GET), path (/a/b), protocol (HTTP/1.1)
# or None, if the request is malformed
def parse_req(req):
    res = re.match('^([^ ]+) (.+) ([^ ]+)$', req)
    return res.groups()

# given a raw URI from client,
# returns 2-member tuple - "protocol" and sanitized path
# (ie. /tar://a/b would return [0] == "tar", [1] == "a/b",
#      just /a/b  would return [0] == "" , [1] == "a/b")
def parse_uri(uri):
    # find protocol
    proto = re.match('/(.+)://', uri)
    if proto:
        proto = proto.group(1)
        uri = re.split('/.+:/', uri, 1)[1]

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

    def getall(self):    # debug-only?
        return self.hdrs

# UNUSED (but working)
#    def add(self, name, value):
#        if not name or not value:
#            return False
#        self.hdrs += [tuple([name.strip(), value.strip()])]

#    def write(self, f):
#        for i in self.hdrs:
#            f.write(i[0] + ': ' + i[1] + '\r\n')

# the actual request-handling class, instantiated for each client
class req_handler(SocketServer.StreamRequestHandler):

    # send an error message to the client (which will still show up
    # as regular stdout data) and forcibly close the connection using
    # (hopefully) TCP RST packet, resulting in a connection reset error
    def error_close(self, msg='invalid request'):
        self.request.send(msg + '\n')
        self.request.setsockopt(socket.SOL_SOCKET, socket.SO_LINGER,
                                struct.pack('ii', 1, 0))
        self.request.close()

    # curl and/or others need \r\n at the end
    def send_response(self, msg):
        self.wfile.write(msg + '\r\n')


    # send a listing of the current working directory,
    # append '/' to directory entries
    def sendfilelist(self, path):
        try:
            contents = os.listdir(path)
        except OSError as err:
            self.error_close('error listing directory: ' + err.strerror)
            return False

        contents.sort()

        # append '/' to directories
        for i,j in enumerate(contents):
            if os.path.isdir(path + '/' + j):
                contents[i] += '/'

        # stringify
        contents = '\n'.join(str(n) for n in contents)
        contents += '\n'

        self.request.send(contents)

    # send a single (regular) file, identified by path, to the client
    def sendfile(self, path):
        chunksize = 65536
        try:
            fd = open(path, 'r');
        except IOError as err:
            self.error_close('error sending file: ' + err.strerror)
            return False

        while 1:
            buff = fd.read(chunksize)
            if not buff:
                break
            self.request.send(buff)


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
            error_close()

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

#        print "reqtype: " + reqtype + ", path: " + path
#        if proto:
#            print "(proto: " + proto + ")"
#        for hdr in head.all():
#            print "hdrname: " + hdr[0] + ", hdrdata: " + hdr[1]
#        print


    def handle_GET(self):
        if os.path.isdir(self.path):
            # if requested path is directory, print its contents
            self.sendfilelist(self.path)
        else:
            # send just the regular file
            self.sendfile(self.path)

#        print 'success!\n'


    def handle_PUT(self):
        chunksize = 65536

        size = self.head.get('Content-Length')
        if not size:
            self.error_close()

        size = int(size)

        self.send_response("HTTP/1.1 100 Continue")

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

        self.send_response("HTTP/1.1 200 OK")
#        print 'finished!'
#        return


# server class, just to re-define SO_REUSEADDR
# and "kill all threads (do not wait)" option on interrupt (ie. SIGINT)
class usable_tcp_server(SocketServer.ThreadingTCPServer):
    allow_reuse_address = True
    daemon_threads = True


def main():
    # chroot to server's servedir
    os.chroot(os.getcwd())

    s_addr = ('', 80)
    server = usable_tcp_server(s_addr, req_handler)
    server.serve_forever()

main()
