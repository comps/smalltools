#!/usr/bin/env python3

import sys, os
import platform
import re
import subprocess
import shutil

sys.path.insert(0, '/home/jjaburek/gitit/pexen')
sys.path.insert(0, '/root/pexen')

from pexen import sched, factory

class BeakerlibFactory(factory.FilesystemFactory):
    @staticmethod
    def read_makefile(dirpath):
        makefile = os.path.join(dirpath, 'Makefile')
        if not os.path.exists(makefile):
            return {}
        pat = re.compile('\s+@echo "([^"]+):\s+([^"]*)"')
        newinfo = {}
        with open(makefile) as fobj:
            for line in fobj:
                m = pat.match(line)
                if m:
                    key, val = m.groups()
                    if not key in newinfo:
                        newinfo[key] = val
                    else:
                        newinfo[key] += ' ' + val
        return newinfo

    @staticmethod
    def format_infotime(natural):
        """Produce datetime.time from '10m', '1h23m', '1d12h', etc."""
        pass  # TODO

    def wrap_executable(self, dirpath, fname):
        full = os.path.join(dirpath, fname)
        testinfo = self.read_makefile(dirpath)  # TODO: reuse from valid_executable
        # TODO: process test metadata further
        def run_exec():
            p = subprocess.run([fname],
                               cwd=dirpath,
                               executable=os.path.abspath(full),
                               check=True,
                               **self.extra_args)
            return p
        # ignore failures, for now
        run_exec = sched.task_fail_wrap(run_exec)
        # without runtest.sh, just the dirname
        self.callpath_burn(run_exec, path=dirpath.strip('/').split('/'))
        # simple global lock; concurrent tasks as r-o, others r-w
        if self.concurrent(full):
            sched.meta.assign_val(run_exec, uses=['everything'])
        else:
            sched.meta.assign_val(run_exec, claims=['everything'])
        # for query during result processing
        if testinfo:
            setattr(run_exec, 'testinfo', testinfo)
        return run_exec

    def valid_executable(self, dirpath, fname):
        testinfo = self.read_makefile(dirpath)
        full = os.path.join(dirpath, fname)
        if fname != 'runtest.sh':
            return False
        if not os.access(full, os.X_OK):
            return False
        if not self.relevant(testinfo) or self.destructive(testinfo, full):
            return False
        return True

    @staticmethod
    def relevant(testinfo):
        distro = ver = major = None
        with open('/etc/os-release') as f:  # TODO: optimize, don't open for each test
            for line in f:
                if line.startswith('ID='):
                    distro = line.split('=',1)[1].strip('"\n')
                if line.startswith('VERSION_ID='):
                    ver = line.split('=',1)[1].strip('"\n')
                    major = ver.split('.',1)[0]
        if not distro or not major:
            return False
        if 'Releases' in testinfo:
            nodistro = f'-{distro.upper()}{major}'  # '-RHEL7'
            if nodistro in testinfo['Releases'].split():
                return False
        if 'Architectures' in testinfo:
            arch = platform.uname().machine
            if not arch.lower() in testinfo['Architectures'].lower().split():
                return False
        return True

    @staticmethod
    def destructive(testinfo, runtest):
        if 'Destructive' in testinfo:
            if testinfo['Destructive'] != 'no':
                return True
        with open(runtest) as f:
            for line in f:
                if 'rhts-reboot' in line:
                    return True  # "destructive" for non-harness execution
        return False

    @staticmethod
    def concurrent(runtest):
        """Return True if the test doesn't need exclusive global lock."""
        with open(runtest) as f:
            prohibited = ['rlService', 'rlRpmInstall', 'rlFetchSrcForInstalled',
                          'rlMount', 'rlFileBackup', 'semanage', 'rngd', 'mssh']
            for line in f:
                if any(x in line for x in prohibited):
                    return False
        return True


def required_rpms(tasks):
    """Given an iterable of task/callables, collect the full list of rpms
    required by them, to be installed via yum/dnf."""
    libpat = re.compile('[Ll]ibrary\(([^)]+)\)')
    rpms = set()
    for task in tasks:
        if hasattr(task, 'testinfo'):
            if 'Requires' in task.testinfo:
                rpms.update(task.testinfo['Requires'].split())
            if 'RhtsRequires' in task.testinfo:
                rpms.update(task.testinfo['RhtsRequires'].split())
#                for req in task.testinfo['RhtsRequires'].split():
#                    m = libpat.match(req)
#                    if m:
#                        parts = m.groups()[0].split('/')
#                        rpms.add('*{0}-Library-{1}'.format(*parts))
#                    else:
#                        rpms.add(req)

    return rpms


if __name__ == '__main__':
    if len(sys.argv) < 2:
        sys.exit(1)

    basepath = sys.argv[1]

    # TODO: ./pyness.py some/actual/path/tab/complete/compatible some/actual/path
    #                     - would start callpath at tab/complete/compatible

    f = BeakerlibFactory(stdout=subprocess.PIPE, stderr=subprocess.PIPE)
    funcs = list(f(basepath))

    print(len(funcs))

    funcs = list(filter(lambda x: BeakerlibFactory.concurrent('/'.join(factory.get_callpath(x))+'/runtest.sh'),
                        funcs))

    for func in funcs:
        meta = sched.meta.retrieve_meta(func)
        print(func)
        print(f"    ::: {meta}")
        print("--------------------------------------")

    rpms = required_rpms(funcs)
    print("Installing:", ', '.join(rpms))

    if shutil.which('dnf'):
        subprocess.check_call(['dnf', '-y', '--setopt=strict=false',
                               'install'] + list(rpms))
    else:
        subprocess.check_call(['yum', '-y', 'install'] + list(rpms))


    s = sched.Sched(funcs)
    p = sched.pool.ThreadWorkerPool(workers=40)
    for res in s.run(poolinst=p):
        cp = factory.get_callpath(res.task)
        print("Finished", '/'.join(cp))
        #print(func.testinfo)
        #print(res)

        # TODO: rewrite as open above BeakerlibFactory() + pass fobj instead of PIPE

        # TODO: have to check if res.excinfo exists, because if it does, res.ret==None
        #if res.ret.stdout:
        #    with open(os.path.join('logs',cp[-1])+'.stdout', 'wb') as fo:
        #        fo.write(res.ret.stdout)
        #if res.ret.stderr:
        #    with open(os.path.join('logs',cp[-1])+'.stderr', 'wb') as fo:
        #        fo.write(res.ret.stderr)
