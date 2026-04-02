#!/bin/bash
# fix_commits.sh — Rewrite author/committer on two specific commits
# Run via Git Bash: bash fix_commits.sh

FILTER_BRANCH_SQUELCH_WARNING=1 git filter-branch -f --env-filter '

if [ "$GIT_COMMIT" = "e4881e2c42f811e9b1654d48a88cdf21e2988bcd" ]; then
    GIT_AUTHOR_NAME="brunda933"
    GIT_AUTHOR_EMAIL="pes1ug23cs151@pesu.pes.edu"
    GIT_COMMITTER_NAME="brunda933"
    GIT_COMMITTER_EMAIL="pes1ug23cs151@pesu.pes.edu"
fi

if [ "$GIT_COMMIT" = "746469ba0e60f0369bb3c97dcc0fb02a70dca073" ]; then
    GIT_AUTHOR_NAME="Chinmay-13"
    GIT_AUTHOR_EMAIL="chinnumuragod876@gmail.com"
    GIT_COMMITTER_NAME="Chinmay-13"
    GIT_COMMITTER_EMAIL="chinnumuragod876@gmail.com"
fi

export GIT_AUTHOR_NAME GIT_AUTHOR_EMAIL GIT_COMMITTER_NAME GIT_COMMITTER_EMAIL

' HEAD

echo ""
echo "Done! Now run: git push --force-with-lease"
