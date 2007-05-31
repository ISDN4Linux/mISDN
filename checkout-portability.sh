#!/bin/bash

#checks out the mISDN_1_1 branch
git-checkout -b mISDN_1_1 origin/mISDN_1_1
git-config branch.mISDN_1_1.remote origin
git-config branch.mISDN_1_1.merge refs/heads/mISDN_1_1
