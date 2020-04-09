#!/usr/bin/env python3
#
# Copyright (c) 2020 Red Hat, Inc. All rights reserved.
#
# This program is free software: you can redistribute it and/or modify
# it under the terms of the GNU General Public License as published by
# the Free Software Foundation, either version 3 of the License, or
# (at your option) any later version.
#
# This program is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
# GNU General Public License for more details.
#
# You should have received a copy of the GNU General Public License
# along with this program. If not, see <http://www.gnu.org/licenses/>.
#

import sys, os
import re
import subprocess
import json
import requests
import logging
import time
import argparse
import textwrap
from datetime import datetime, timedelta

class StateFile:
    tmp = '.tmp'
    def __init__(self, path):
        self._checkperm(path)
        self._checkperm(path+self.tmp)
        self.path = path

    @staticmethod
    def _checkperm(path):
        with open(path, 'a+') as f:
            pass

    def save(self, data):
        with open(self.path+self.tmp, 'w') as f:
            json.dump(data, f, indent=2)
        os.replace(self.path+self.tmp, self.path)

    def load(self):
        with open(self.path, 'r') as f:
            try:
                return json.load(f)
            except json.decoder.JSONDecodeError:
                return dict()

class JobKeeper:
    def __init__(self, hub, jobfile, statefile=None, resched=None,
                 nosys_delay=None, exportdir=None):
        self.session = requests.Session()
        self.session.headers.update({'Accept': 'application/json'})
        self.hub = hub
        self.jobfile = jobfile
        self.state = None
        self.running = set()
        self.queued = None
        self.queued_time = None
        self.queued_delta_max = None
        self.nosys_time = None
        self.nosys_delta_max = None
        self.exportdir = exportdir
        if resched:
            self.queued_delta_max = timedelta(seconds=resched)
        if nosys_delay:
            self.nosys_delta_max = timedelta(seconds=nosys_delay)
        if statefile:
            self._load_state(statefile)
        if exportdir:
            if not os.path.isdir(exportdir) or not os.access(exportdir, os.W_OK):
                raise RuntimeError(f"cannot create files in {exportdir}")

    def _load_state(self, path):
        self.state = StateFile(path)
        data = self.state.load()
        timefmt = '%Y-%m-%dT%H:%M:%S.%f'
        if 'queued' in data:
            self.queued = data['queued']
        if 'queued_time' in data and data['queued_time'] is not None:
            self.queued_time = datetime.strptime(data['queued_time'], timefmt)
        if 'nosys_time' in data and data['nosys_time'] is not None:
            self.nosys_time = datetime.strptime(data['nosys_time'], timefmt)
        if 'running' in data:
            self.running = set(data['running'])

    def _save_state(self):
        timefmt = '%Y-%m-%dT%H:%M:%S.%f'
        if self.state:
            data = {}
            data['queued'] = self.queued
            if self.queued_time is not None:
                data['queued_time'] = self.queued_time.strftime(timefmt)
            if self.nosys_time is not None:
                data['nosys_time'] = self.nosys_time.strftime(timefmt)
            data['running'] = list(self.running)
            self.state.save(data)

    def _submit_job(self):
        # read jobfile from disk on every submit, in case it changed
        res = subprocess.run(['bkr', 'job-submit', self.jobfile], shell=False,
                             stdout=subprocess.PIPE, universal_newlines=True)
        if res.returncode not in [0,1]:
            # uncommon, ie. command not found, raise an error
            res.check_returncode()
        if res.returncode == 1:
            # treat common errors as transient
            return None
        # when given bad XML, 'bkr job-submit' sometimes returns empty []
        # and exits with 0
        if 'Submitted: []' in res.stdout:
            raise RuntimeError("bkr job-submit returned empty [], check xml")
        m = re.match('Submitted: \[\'J:([0-9]+)\'\]', res.stdout)
        if not m:
            raise RuntimeError("failed parsing J:id from bkr job-submit: "+\
                               res.stdout)
        return int(m.group(1))

    @staticmethod
    def _cancel_job(jobid):
        res = subprocess.run(['bkr', 'job-cancel', f'J:{jobid}'], shell=False,
                             stdout=subprocess.PIPE)
        if res.returncode not in [0,1]:
            # uncommon, ie. command not found, raise an error
            res.check_returncode()

    def _export_job(self, jobid, jdata):
        if self.exportdir:
            with open(os.path.join(self.exportdir, f'{jobid}.json'), 'w') as f:
                json.dump(jdata, f, indent=2, default=str)

    def _query_bkr(self, idval, idtype='jobs'):
        try:
            r = self.session.get(self.hub+f'/{idtype}/{idval}')
            r.raise_for_status()
        except requests.exceptions.RequestException as e:
            print(str(e), file=sys.stderr)
            return None
        return r.json()

    def _aborted_with_nosystems(self, jdata):
        """Return True if any task in any recipe of the job did not match any
        systems."""
        for rs in jdata['recipesets']:
            for r in rs['machine_recipes']:
                if r['status'] != 'Aborted':
                    continue
                rdata = self._query_bkr(r['id'], 'recipes')
                if rdata:
                    nosystems = False
                    if rdata['status'] != 'Aborted':
                        continue
                    for t in rdata['tasks']:
                        if t['status'] != 'Aborted':
                            continue
                        for res in t['results']:
                            if 'does not match any systems' in res['message']:
                                return True
        return False

    def _queued_overtime(self):
        if self.queued_time is None or self.queued_delta_max is None:
            return False
        if datetime.utcnow() - self.queued_time > self.queued_delta_max:
            return True
        return False

    def _nosys_wait(self):
        if self.nosys_time is None or self.nosys_delta_max is None:
            return False
        if datetime.utcnow() - self.nosys_time < self.nosys_delta_max:
            return True
        return False

    def watch_jobs(self, max_running, delay, noloop=False, nosys_suppress=False):
        while True:
            logging.debug(f"========================================")
            running_cnt = len(self.running)
            one_sleep = delay/max(1,running_cnt)
            # process running jobs, yield any completed ones
            logging.debug(f"iterating through {running_cnt} running jobs")
            for job in self.running.copy():
                logging.debug(f"looking into running job {job}")
                jdata = self._query_bkr(job)
                if jdata:
                    finished = jdata['is_finished']
                    status = jdata['status']
                    result = jdata['result']
                    logging.debug(f"running job {job} is finished:{finished} with {status}/{result}")
                    if finished:
                        self.running.remove(job)
                        aborted_nosys = self._aborted_with_nosystems(jdata)
                        if aborted_nosys:
                            logging.info(f"running job {job} aborted with nosystems")
                            self.nosys_time = datetime.utcnow()
                        if not aborted_nosys or (aborted_nosys and not nosys_suppress):
                            self._export_job(job, jdata)
                            logging.info(f"yielding finished {(job, status, result)}")
                            yield (job, status, result)
                        self._save_state()
                logging.debug(f"sleeping for {one_sleep:.2f} sec")
                time.sleep(one_sleep)
            # check on our queued job - if it's running, add it to running
            if self.queued is not None:
                jdata = self._query_bkr(self.queued)
                logging.debug(f"queued job {self.queued} is {jdata['status']}")
                if jdata['status'] not in ['New','Processed','Queued']:
                    logging.info(f"moving queued job {self.queued} to running queue")
                    self.running.add(self.queued)
                    self.queued = None
                    self.queued_time = None
                    self._save_state()
                else:
                    # cancel queued job if it's taking too long
                    if self._queued_overtime():
                        logging.info(f"queued job {self.queued} took too long, cancelling it")
                        self._cancel_job(self.queued)
            # if we were unable (yet) to schedule a Queued job, try now
            if self.queued is None:
                logging.debug(f"queued job is null, running:{len(self.running)} < max:{max_running}")
                if len(self.running) < max_running:
                    if self._nosys_wait():
                        logging.debug(f"nosys timeout in effect, NOT submitting new Queued job")
                    else:
                        self.queued = self._submit_job()
                        self.queued_time = datetime.utcnow()
                        self.nosys_time = None
                        self._save_state()
                        logging.info(f"queued job submitted: {self.queued}")
            # if we didn't sleep in self.running loop, sleep now
            if running_cnt == 0:
                logging.debug(f"sleeping for {one_sleep:.2f} sec")
                time.sleep(one_sleep)
            # user requested single-pass
            if noloop:
                break

def guess_hub_url():
    """Guess Beaker hub URL."""
    def _read_cfg(path):
        try:
            with open(path) as f:
                for line in f:
                    m = re.match('HUB_URL = "?([^"]+)"?', line)
                    if m:
                        return m.group(1)
        except FileNotFoundError:
            pass

    home = os.environ.get('HOME')
    if home:
        url = _read_cfg(os.path.join(home, '.beaker_client/config'))
        if url:
            return url
    return _read_cfg('/etc/beaker/client.conf')


if __name__ == '__main__':
    epilog = textwrap.dedent("""
        Jobkeeper (this tool) is used to ensure that at least one instance of a job xml
        is always Queued and available to the Beaker scheduler.
        This assumes that the job xml specifies a hostRequires condition that makes it
        stay Queued most of the time, switching to Running when a suitable machine
        becomes available.

        The core task of jobkeeper is to watch for this Queued->Running transition and
        submit a new Queued job, waiting for another suitable machine.

        When the amount of Running jobs reaches --max-running, no new jobs are Queued
        until at least one of the Running jobs finishes.

        To lessen the load on Beaker hub, --sleep seconds of extra delay are inserted
        during one checking cycle, gradually, to distribute the load.

        To keep track of Queued/Running jobs and resume operation after an interrupt
        (program exit), store the current state in a --state file.

        The job xml is re-read before every submission, allowing you to change it on
        the fly. If this is not enough, use --oneshot and an external bash 'while true'
        loop. In this mode, jobkeeper exits after one checking cycle, allowing you to
        perform house-keeping tasks (or changing job xml) between cycles.
        Note that --oneshot doesn't make sense without --state.

        To prevent the Queued job from becoming stale (ie. with a hostRequire condition
        matches a long-time Loaned system), cancel and re-submit the job --re-sched
        seconds after the initial submission. As stated above, this re-reads and submits
        a fresh job xml.

        Similarly, if a hostRequire condition doesn't match any systems, the job Aborts
        with 'No matching system(s) found'. Use --nosys-delay to suspend submitting of
        a new Queued task for a while (ie. until your script updates job xml).
        Note that it takes 1 cycle for a Queued job to be moved to a Running list and
        1 another cycle to process its result, so there will always be one extra Queued
        job aborted, submitted during the first cycle.

        Errors/warnings/debug go to stderr, finished jobs to stdout as space-delimited
        triples of jobid, status and result, ie. '12345 Completed Fail'.

        For more details about finished job, use --export with a pre-existing directory
        into which ${jobid}.json files will be saved before being reported on stdout.

        To avoid 'No matching system(s) found' jobs on stdout and in --export, use
        --nosys-suppress. Useful for filtering out common Aborts.
        """)
    parser = argparse.ArgumentParser(description='Keep a Beaker job always available (Queued).',
                                     epilog=epilog,
                                     formatter_class=argparse.RawTextHelpFormatter)
    parser.add_argument('-v', '--verbose', action='count', default=0, help='increase verbosity, -vv for debug')
    parser.add_argument('--state', help='path where to store a persistent metadata file')
    parser.add_argument('--oneshot', action='store_true', help='do only one loop cycle, then exit')
    parser.add_argument('--sleep', metavar='N', type=int, default=60, help='extra secs spent sleeping in each loop')
    parser.add_argument('--max-running', metavar='N', type=int, default=2, help='maximum allowed Running jobs at any time')
    parser.add_argument('--re-sched', metavar='N', type=int, help='cancel and re-submit Queued task after N secs')
    parser.add_argument('--nosys-delay', metavar='N', type=int, help='don\'t submit Queued task N sec after \'No systems found\'')
    parser.add_argument('--nosys-suppress', action='store_true', help='don\'t output nosys-Aborted task details on stdout')
    parser.add_argument('--export', metavar='DIR', help='export completed jobs as JSON files')
    parser.add_argument('--hub', metavar='URL', help='override autodetected Beaker hub URL')
    parser.add_argument('jobfile', metavar='job-file.xml', help='a Beaker job XML file path')

    args = parser.parse_args()

    if args.oneshot and not args.state:
        print("error: --oneshot doesn't make sense without --state", file=sys.stderr)
        sys.exit(1)

    if not args.hub:
        args.hub = guess_hub_url()
        if not args.hub:
            raise RuntimeError("unable to get Beaker hub URL, try --hub")

    logging_fmt = '%(asctime)s %(levelname)s: %(message)s'
    logging_datefmt = '%Y-%m-%d %H:%M:%S'
    if args.verbose >= 2:
        logging.basicConfig(level=logging.DEBUG,
                            format=logging_fmt,
                            datefmt=logging_datefmt)
        logging.getLogger('urllib3').setLevel(logging.WARNING)
    elif args.verbose == 1:
        logging.basicConfig(level=logging.INFO,
                            format=logging_fmt,
                            datefmt=logging_datefmt)

    jk = JobKeeper(statefile=args.state,
                   resched=args.re_sched,
                   nosys_delay=args.nosys_delay,
                   jobfile=args.jobfile,
                   exportdir=args.export,
                   hub=args.hub)

    for ret in jk.watch_jobs(delay=args.sleep,
                             max_running=args.max_running,
                             noloop=args.oneshot,
                             nosys_suppress=args.nosys_suppress):
        if ret:
            print(' '.join(str(x) for x in ret))
