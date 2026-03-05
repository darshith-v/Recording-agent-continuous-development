#!/bin/bash

# @author rshafiq

# Local Build Script for making Reagent Packages on your Local Machine


print(){
	echo -e "\e[1;31m$1 \e[0m"
}

if [ -z "$RA_TARGET" ] ; then
   RA_TARGET=x86_64-linux-gnu
   RA_BUILD_DIR=$RA_TARGET
   CROSS_PFX=
   CROSS_SFX=
   CROSS_TOOLCHAIN_BASE=
   CROSS_XTRA_PATH=
fi

# Change directories to the top level
cd ..

# Set the build target if it is not already set
TARGET=$(source ./set_build_target)
echo "$TARGET"

if [[ "$TARGET" =~ "*** Build Target is not set." || "unknown Build Target." ]]; then
	#build target is not set
	echo "BUILD TARGET IS NOT SET"
	echo "$TARGET"
	echo "Do you wish to proceed with Build Target set to $RA_TARGET (Y/N)?: "
	read answer

	if [[ "$answer" != "${answer#[Yy]}" ]] ; then
		echo "Setting build target to $RA_TARGET."
		source ./set_build_target $RA_TARGET
	else
		echo "Exiting.  Please run 'source ./set_build_target <target>', then retry."
		exit 2
	fi
else
	#build target is set
	echo "Build Target is set. Starting build."
fi

# Handle Version Number
if [ -z "$1" ]
  then
    echo "Version Number is not supplied supplied"
    echo "version number must be in this form xx.xx.xx.xx"
    exit 1
fi

if [[ $1 =~ ^[0-9]{2}\.([0-9]{2})\.([0-9]{2})\.([0-9]{2})$ ]]; then 
	sed -i "s/Version:.*/Version: $1/" package/$RA_BUILD_DIR/reagent/DEBIAN/control
	sed -i "s/RECORDING_AGENT_VERSION =.*/RECORDING_AGENT_VERSION = \"$1\"/" RestfulAPI/site/lib/global_defs.lua

	print "Starting reagent build with version $1."
	#exit 0
else
	echo "Invalid version number"
	echo "version number must be in this form xx.xx.xx.xx"
	exit 1
fi

echo "Reagent build script"
LOG_DIR=/tmp/build_log_$(date +%Y_%m_%d)
mkdir -p $LOG_DIR
LOG_FILE=$LOG_DIR/ReagentBuild_$(date +%Y_%m_%d_%H_%M_%S).log
echo "Logfile: $LOG_FILE"
echo -e "\e[0;32m ====================================================================== \e[0m"
# open fd=3 redirecting to 1 (stdout)
exec 3>&1

# redirect stdout/stderr to a file but show stderr on terminal
exec >$LOG_FILE 2> >(tee >(cat >&3))

# function echo to show echo output on terminal
echo() {
   # call actual echo command and redirect output to fd=3
   command echo "$@" >&3
}

if [ ! -z "$2" ]; then
	#clean all 
	print "cleaning all..."
	make clean_all
fi

print "Create source package..."
make src_pkg
mv ./reagent_src_pkg.zip AutoBuild/reagent_37043_$1.zip

print "Making depend..."
make depend
print "Making rtsprec..."
make rtsprec
print "Making rtspplay..."
make rtspplay
print "Making metarec..."
make metarec
print "Making metacollector..."
make metacollector
print "Making dirworker..."
make dirworker
print "Making reagent..."
make reagent
print "Making access_data..."
make access_data
print "Making recording_list..."
make recording_list
print "Making space_monitor..."
make space_monitor
print "Making validate_drive..."
make validate_drive

print "\nReagent build successful\nMaking debian package..."
#make debain package
make pkg

#rename reaget.deb
mv ./reagent_pkg_$RA_TARGET.deb AutoBuild/reagent_37043_$1.deb
print "\nReagent Build is ready: reagent_37043_$1.deb"
echo "Build Script completed!"
echo ""
echo "see $LOG_FILE for details"
echo ""

# close fd=3
exec 3>&-
exit 0


