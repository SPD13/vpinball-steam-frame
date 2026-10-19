#!/usr/bin/env python3
# Folds the 'fixup!' commits of the fork into their commits, before a rebase on upstream. Called by sync-upstream.sh.
#
# A plain 'git rebase --autosquash' moves each fixup back to the commit it names, ahead of the commits in between, and conflicts when one
# of them touched the same file (or creates the file). So each fixup is first split by file, without touching the working tree, and the
# part of each file goes to the commit which last touched that file: the edits of every file stay in the same order, and folding cannot
# conflict. The fold is checked to give the same tree.
#
# Usage: steamframe/tools/fold-fixups.py <base>     (the commits of base..HEAD; the working tree must be clean)

import os
import subprocess
import sys
import tempfile


def git(*args, env=None):
    return subprocess.run(('git',) + args, check=True, capture_output=True, env=env).stdout.decode('utf-8', 'surrogateescape')


def main(base):
    commits = git('rev-list', '--reverse', '--topo-order', f'{base}..HEAD').split()
    if git('rev-list', '--merges', f'{base}..HEAD').strip():
        sys.exit('Merge commits in the series')
    subject_of = {c: git('log', '-1', '--format=%s', c).rstrip('\n') for c in commits}
    if not any(s.startswith('fixup! ') for s in subject_of.values()):
        print('No fixup commit to fold')
        return
    targets = [s for s in subject_of.values() if not s.startswith('fixup! ')]
    if len(targets) != len(set(targets)):
        sys.exit('Two commits have the same subject: a fixup could go to the wrong one')

    tip_tree = git('rev-parse', 'HEAD^{tree}').strip()
    last_target = {} # File -> subject of the commit its last edit goes to
    prev = git('rev-parse', base).strip()
    for c in commits:
        subject = subject_of[c]
        message = git('log', '-1', '--format=%B', c)
        env = dict(os.environ)
        for name, fmt in (('AUTHOR_NAME', '%an'), ('AUTHOR_EMAIL', '%ae'), ('AUTHOR_DATE', '%aI'), ('COMMITTER_NAME', '%cn'), ('COMMITTER_EMAIL', '%ce'), ('COMMITTER_DATE', '%cI')):
            env['GIT_' + name] = git('log', '-1', '--format=' + fmt, c).rstrip('\n')
        files = git('diff-tree', '--no-commit-id', '--name-only', '-r', '--no-renames', '-z', c).split('\0')[:-1]
        if not subject.startswith('fixup! '):
            for f in files:
                last_target[f] = subject
            prev = git('commit-tree', c + '^{tree}', '-p', prev, '-m', message, env=env).strip()
            continue
        named = subject[len('fixup! '):]
        if named not in targets:
            sys.exit('No commit for: ' + subject)
        groups = {}
        for f in files:
            last_target[f] = last_target.get(f, named)
            groups.setdefault(last_target[f], []).append(f)
        ordered = sorted(groups, key=lambda t: t != named) or [named]
        for i, target in enumerate(ordered):
            tree = c + '^{tree}'
            if i < len(ordered) - 1: # The last part takes the tree of the fixup, which holds all of them
                with tempfile.TemporaryDirectory() as folder:
                    index_env = dict(env, GIT_INDEX_FILE=folder + '/index')
                    git('read-tree', prev, env=index_env)
                    entries = b''
                    for f in groups[target]:
                        path = f.encode('utf-8', 'surrogateescape')
                        entry = subprocess.run(['git', 'ls-tree', '-z', c, '--', f], check=True, capture_output=True).stdout
                        if entry:
                            mode, _, sha = entry.split(b'\t', 1)[0].split()
                            entries += b'%s %s\t%s\0' % (mode, sha, path)
                        else:
                            entries += b'0 ' + b'0' * 40 + b'\t' + path + b'\0'
                    subprocess.run(['git', 'update-index', '-z', '--index-info'], check=True, env=index_env, input=entries)
                    tree = git('write-tree', env=index_env).strip()
            prev = git('commit-tree', tree, '-p', prev, '-m', 'fixup! ' + target, env=env).strip()

    git('reset', '-q', '--hard', prev)
    env = dict(os.environ, GIT_SEQUENCE_EDITOR='true')
    subprocess.run(['git', 'rebase', '-q', '-i', '--autosquash', '--empty=keep', base], check=True, env=env)
    if git('rev-parse', 'HEAD^{tree}').strip() != tip_tree:
        sys.exit('The folded series does not give the same tree')
    print(f'{len(commits)} commits folded into {git("rev-list", "--count", f"{base}..HEAD").strip()}, same tree')


if __name__ == '__main__':
    main(sys.argv[1])
