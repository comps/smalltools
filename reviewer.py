#!/usr/bin/python
#
# this script was used for git branch review, where we had a reference branch
# (master) to review - each team member did their own review on a copy of this
# branch and was allowed to only squash/edit/split their own commits within
# the same region where the original commit was (ie. without moving commits
# across "borders" denoted by commits owned by other people)
#
# this resulted in a set of branches, master + several "user" branches, with
# consistent alternating between authors (same as master), but with different
# commit count (or hashes)
#
# given 3 users, [jj,mv,om], the algorithm below does basically this:
#  - find a commit on master, it is owned by jj
#  - pick all jj-owned commits from jj's user branch, up to the first
#    non-jj-owned commit on his branch
#
#  - skip all consecutive commits on master owned by jj
#  - find a commit on master, it is owned by mv
#  - skip all jj-owned commits on mv's user branch, pick all mv-owned commits
#    from there up to the first non-mv-owned commit on mv's user branch
#
#  - skip all consecutive commits on master owned by mv
#  - ...

import sys, os
import subprocess

def sane_repo():
    res = subprocess.check_output(["git", "status", "--porcelain"])
    return True if res == '' else False

sane_authors = []
def get_author(commit):
    res = subprocess.check_output(
        ["git", "log", "-1", "--format=format:%ae", commit])
    if res not in sane_authors:
        raise ValueError("insane author detected: %s" % res)
    return res

def checkout(commit):
    subprocess.check_call(["git", "checkout", commit+"^0"])
def cherry_pick(commit):
    subprocess.check_call(["git", "cherry-pick", "--allow-empty", commit])

def iter_commits(base, branch):
    proc = subprocess.Popen(
        ["git", "rev-list", "--reverse", base+".."+branch],
        stdout=subprocess.PIPE)
    for line in iter(proc.stdout.readline,''):
        yield line.rstrip()

# pop all leading commits that belong to a specific author from a list
def pop_leading(tlist, author):
    while len(tlist) > 0:
        if get_author(tlist[0]) != author:
            break
        yield tlist.pop(0)

def main():
    if len(sys.argv) < 5:
        sys.exit(1)

    base = sys.argv[1]    # commit base for all
    master = sys.argv[2]  # reference branch
    user_branches = []    # args of user@email/branchname
    for arg in sys.argv[3:]:
        (user, branch) = arg.split('/', 1)
        user_branches.append((user, branch))

    if not sane_repo():
        sys.exit(2)

    # fill in sane_authors with users/authors read from cmdline
    global sane_authors
    sane_authors = [x[0] for x in user_branches]

    # for each user, get a list of commits in their user branch
    user_commits = {}
    for user, branch in user_branches:
        user_commits[user] = list(iter_commits(base, branch))

    # get list of unique (alternating) authors from the reference branch
    author_order = []
    author = ''
    for c in iter_commits(base, master):
        newauthor = get_author(c)
        if newauthor != author:
            author_order.append(newauthor)
            author = newauthor

    # go over the alternating list and reconstruct a new history built from
    # the user branches, with commits for each author (user) taken from their
    # own branch
    new_master = []
    for author in author_order:
        # pick that author's commits
        popped = list(pop_leading(user_commits[author], author))
        if len(popped) < 1:
            raise AssertionError("author ref has no commits for %s" % author)
        new_master += popped
        # remove the author's commits from all other branches
        for user, branch in user_commits.iteritems():
            if user == author:
                continue
            if len(list(pop_leading(branch, author))) < 1:
                raise AssertionError("user %s ref has no commits for %s"
                                    % (user, author))

    # reconstruct a new branch from the selected commits
    checkout(base)
    for c in new_master:
        cherry_pick(c)

if __name__ == "__main__":
    main()
