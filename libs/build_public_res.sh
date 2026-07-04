#!/bin/bash
set -e

source libs/env_deploy.sh
DEST=$DEPLOYMENT/public_res
rm -rf $DEST
mkdir -p $DEST

#### Download geodata ####
RULES_BASE="https://raw.githubusercontent.com/runetfreedom/russia-v2ray-rules-dat/release"
curl -fLso $DEST/geoip.dat "$RULES_BASE/geoip.dat"
curl -fLso $DEST/geosite.dat "$RULES_BASE/geosite.dat"
curl -fLso $DEST/geoip.dat.sha256sum "$RULES_BASE/geoip.dat.sha256sum"
curl -fLso $DEST/geosite.dat.sha256sum "$RULES_BASE/geosite.dat.sha256sum"
(cd "$DEST" && sha256sum -c geoip.dat.sha256sum && sha256sum -c geosite.dat.sha256sum)
rm -f $DEST/geoip.dat.sha256sum $DEST/geosite.dat.sha256sum

#### copy res/public ####
cp res/public/* $DEST
