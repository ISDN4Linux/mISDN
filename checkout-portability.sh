#!/bin/bash

#checks out the mISDN_1_1 branch
git-checkout -b portability origin/portability
git-config branch.portability.remote origin
git-config branch.portability.merge refs/heads/portability
