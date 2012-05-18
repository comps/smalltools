#!/usr/bin/python

import SocketServer
import os, sys
import socket, struct
import tarfile



class usable_tcp_server(SocketServer.ThreadingTCPServer):
    allow_reuse_address = True
    daemon_threads = True

class req_handler(SocketServer.BaseRequestHandler):

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


    def handle(self):
        readmax = 1024

        # receive at most readmax chars
        data = self.request.recv(readmax)
        if not data:
            self.error_close('request too large')
            return

        print "rawdata:", data

        # chop only first line, without trailing spaces
        data = data.split('\n', 1)
        data = data[0].strip()
        if not data:
            self.error_close()
            return

        # len('GET / HTTP/1.X') == 14, begins with 'GET /'
        if len(data) < 14 or not data.startswith('GET /'):
            self.error_close()
            return

        # verify and remove trailing HTTP/1.X
        trail = data.rsplit(' ', 1)
        if not trail[1].startswith('HTTP/1.'):
            self.error_close()
            return

        data = trail[0].split(' ', 1)
        data = data[1]

        # data now contains the actual GET path
        print "data:", data

        if data.find('/..') != -1 or data.find('../') != -1:
            self.error_close()
            return

        # prepend $PWD
        path = os.getcwd() + data;

        if os.path.isdir(path):
            # if requested path is directory, print its contents
            self.sendfilelist(path)
        else:
            # send just the regular file
            self.sendfile(path)

        print 'success!\n'

    def error_close(self, msg='invalid request'):
        self.request.send(msg + '\n')
        self.request.setsockopt(socket.SOL_SOCKET, socket.SO_LINGER,
                                struct.pack('ii', 1, 0))
        self.request.close()


def main():
    s_addr = ('', 8080)
    server = usable_tcp_server(s_addr, req_handler)
    server.serve_forever()

main()
