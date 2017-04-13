#!/usr/bin/env bash
# Adapted from script by Yuriy Tymchuk 2013
# and here https://gist.github.com/domenic/ec8b0fc8ab45f39403dd
# https://sleepycoders.blogspot.com/2013/03/sharing-travis-ci-generated-files.html
TARGET_BRANCH="gh-pages"
set -x
if [ "$TRAVIS_PULL_REQUEST" != "false" -o "$TRAVIS_BRANCH" != "$TARGET_BRANCH"  ]; then
  printf "Starting to update %s\n" "$TARGET_BRANCH"
    # Save some useful information
  REPO=`git config remote.origin.url`
  SSH_REPO=${REPO/https:\/\/github.com\//git@github.com:}
  SHA=`git rev-parse --verify HEAD`
  #copy data we're interested in to other place
  mkdir -p "$HOME/staging"
  # Do MoarVM specific code here
  # --------------------------------
  cp -R $TRAVIS_BUILD_DIR/html-cov/* "$HOME/staging"
  # END MoarVM specific code
  # -----------------------------
  #go to home and setup git
  cd -- "$HOME" || echo "Couldn't cd into $HOME";
  git config user.name "Travis CI"
  #git config user.email "$COMMIT_AUTHOR_EMAIL"
  git config --global user.email "travis@travis-ci.org"
  #git config --global user.name "Travis"

  git clone -v $SSH_REPO $TARGET_BRANCH
  mkdir -p gh-pages
  cd gh-pages
  git checkout $TARGET_BRANCH || git checkout --orphan $TARGET_BRANCH
  # Copy our files from staging to the repo
  cp -Rf $HOME/staging/* .
  #if [ -z `git diff --exit-code` ]; then
  #  printf "No changes to the output on this push; exiting.\n"
  #  exit 0
  #fi
  #add, commit and push files
  ls -lh
  make_index () { $TRAVIS_BUILD_DIR/tools/make-index.sh > ./index.html ; }
  make_index
  git add -fv .
  git commit -m "Travis build $TRAVIS_BUILD_NUMBER pushed to $TARGET_BRANCH" && \
  git pull --rebase --ff-only && \
  git push -fv origin $TARGET_BRANCH

  echo -e "Done magic with push\n"
fi
