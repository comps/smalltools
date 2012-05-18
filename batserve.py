#!/usr/bin/python

#import SocketServer
from SocketServer import ThreadingMixIn
from BaseHTTPServer import HTTPServer, BaseHTTPRequestHandler

from glob import glob

import os, sys
import re
#import socket, struct
import tarfile


#
#
## NOTE: discarded because BaseHTTPServer cannot handle spaces in URL
##       (and does tons of unwanted things automatically)
#
#

class threaded_http_server(HTTPServer, ThreadingMixIn):
    allow_reuse_address = True
    daemon_threads = True

# GET/PUT
# \`-- tar{gz,bz2}://
# \`-- file://
#  `-- else (fallback) --^
#
class req_handler(BaseHTTPRequestHandler):
    def do_GET(self):

        a,b = self.parse_uri(self.path)
        print 'a:', a
        print 'b:', b

#        self.send_error(400)
        self.send_response(200)
#        self.send_header("Location", '/autofile')
        self.end_headers()

#        self.send_response(200)
#        self.send_response(200)
#        self.send_header("Location", '/tv.m3u')
#        self.send_header("Content-Type", "text/plain")
#        self.send_header("Warning", "some warning!")
#        self.end_headers()
#        self.error_close()

    def do_PUT(self):
        # BaseHTTPServer itself should send 100-continue
        data = self.rfile.read(100000)
        print data

    # given a raw URI from client,
    # returns 2-member tuple - "protocol" and sanitized path
    # (ie. /tar://a/b would return [0] == "tar", [1] == "a/b",
    #      just /a/b  would return [0] == "" , [1] == "a/b")
    def parse_uri(self, uri):
        # find protocol
        proto = re.match('/(.+)://', uri)
        if proto:
            proto = proto.group(1)
            uri = re.split('/.+:/', uri, 1)[1]

        # sanitize
        uri = os.path.normpath(uri)
        if os.path.isabs(uri):
            uri = uri[1:]  # remove leading slash

        return (proto, uri)












#    def tar_create(self, compress=None):

#    def tar_extract(self, compress=None):

#    def error_close(self, msg='invalid request'):
#        self.request.send(msg + '\n')
#        self.request.setsockopt(socket.SOL_SOCKET, socket.SO_LINGER,
#                                struct.pack('ii', 1, 0))
#        self.request.close()




#class req_handler_old(SocketServer.BaseRequestHandler):
#
#    def sendfilelist(self, path):
#        try:
#            contents = os.listdir(path)
#        except OSError as err:
#            self.error_close('error listing directory: ' + err.strerror)
#            return False
#
#        contents.sort()
#
#        # append '/' to directories
#        for i,j in enumerate(contents):
#            if os.path.isdir(path + '/' + j):
#                contents[i] += '/'
#
#        # stringify
#        contents = '\n'.join(str(n) for n in contents)
#        contents += '\n'
#
#        self.request.send(contents)
#
#    def sendfile(self, path):
#        chunksize = 65536
#        try:
#            fd = open(path, 'r');
#        except IOError as err:
#            self.error_close('error sending file: ' + err.strerror)
#            return False
#
#        while 1:
#            buff = fd.read(chunksize)
#            if not buff:
#                break
#            self.request.send(buff)
#
#
#    def handle(self):
#        readmax = 1024
#
#        # receive at most readmax chars
#        data = self.request.recv(readmax)
#        if not data:
#            self.error_close('request too large')
#            return
#
#        print "rawdata:", data
#
#        # chop only first line, without trailing spaces
#        data = data.split('\n', 1)
#        data = data[0].strip()
#        if not data:
#            self.error_close()
#            return
#
#        # len('GET / HTTP/1.X') == 14, begins with 'GET /'
#        if len(data) < 14 or not data.startswith('GET /'):
#            self.error_close()
#            return
#
#        # verify and remove trailing HTTP/1.X
#        trail = data.rsplit(' ', 1)
#        if not trail[1].startswith('HTTP/1.'):
#            self.error_close()
#            return
#
#        data = trail[0].split(' ', 1)
#        data = data[1]
#
#        # data now contains the actual GET path
#        print "data:", data
#
#        if data.find('/..') != -1 or data.find('../') != -1:
#            self.error_close()
#            return
#
#        # prepend $PWD
#        path = os.getcwd() + data;
#
#        if os.path.isdir(path):
#            # if requested path is directory, print its contents
#            self.sendfilelist(path)
#        else:
#            # send just the regular file
#            self.sendfile(path)
#
#        print 'success!\n'
#
#    def error_close(self, msg='invalid request'):
#        self.request.send(msg + '\n')
#        self.request.setsockopt(socket.SOL_SOCKET, socket.SO_LINGER,
#                                struct.pack('ii', 1, 0))
#        self.request.close()


def main():
    s_addr = ('', 8080)
#    server = usable_tcp_server(s_addr, req_handler)
    server = threaded_http_server(s_addr, req_handler)
    server.serve_forever()

main()
