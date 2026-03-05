#!/bin/bash

## Get clean reageng source tree from SCM
#
BASEDIR=`pwd`
SRCDIR=RA_pack
APPDIR=ReAgent
SCMUSER=SRV_WREGT_AUTOMATION
SCMPASS='$U5@?yJNmAS)~lp'

SCMWS_PFX=abws

MAINSTREAM=REC_BNSF_DASH9_LDRS-V_Phase2_+Mainline
MAINWS_DESCRIPTION=ReAgent_main_package
MAINWS_UNIQUE=$(openssl rand -base64 12)
MAINWS_NAME=${SCMWS_PFX}_${MAINWS_DESCRIPTION}_${MAINWS_UNIQUE}

mkdir -p ${BASEDIR}
mkdir -p ${BASEDIR}/${SRCDIR}

scm login -r https://clm.wabtec.com/ccm -n local -u ${SCMUSER} -P "${SCMPASS}"

cd ${BASEDIR}/${SRCDIR}

scm create workspace -r local -s "${MAINSTREAM}" -d "${MAINWS_DESCRIPTION}" "${MAINWS_NAME}"
scm load -r local "${MAINWS_NAME}" "REC_WRE_VIDEO_REAGENT" -t .
scm unload -r local -w "${MAINWS_NAME}"
scm delete workspace -r local "${MAINWS_NAME}"
cd ..

scm logout -r local

# perform a full Build
cd ${BASEDIR}/${SRCDIR}/ReAgent
make depend
make rtsprec
make rtspplay
make reagent
make deb

echo "**** ${BASEDIR}/${SRCDIR}/${APPDIR}/package/reagent.deb is ready"
