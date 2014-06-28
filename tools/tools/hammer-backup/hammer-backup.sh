#! /bin/sh
#
# Copyright (c) 2014 The DragonFly Project.  All rights reserved.
#
# This code is derived from software contributed to The DragonFly Project
# by Antonio Huete <tuxillo@quantumachine.net>
#
# Redistribution and use in source and binary forms, with or without
# modification, are permitted provided that the following conditions
# are met:
#
# 1. Redistributions of source code must retain the above copyright
#    notice, this list of conditions and the following disclaimer.
# 2. Redistributions in binary form must reproduce the above copyright
#    notice, this list of conditions and the following disclaimer in
#    the documentation and/or other materials provided with the
#    distribution.
# 3. Neither the name of The DragonFly Project nor the names of its
#    contributors may be used to endorse or promote products derived
#    from this software without specific, prior written permission.
#
# THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
# ``AS IS'' AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
# LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS
# FOR A PARTICULAR PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL THE
# COPYRIGHT HOLDERS OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT,
# INCIDENTAL, SPECIAL, EXEMPLARY OR CONSEQUENTIAL DAMAGES (INCLUDING,
# BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
# LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED
# AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
# OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT
# OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
# SUCH DAMAGE.
#
# set -x
#
# hammer-backup
#
# This script operats HAMMER PFSes and dumps its contents for backup
# purposes.
#

initialization()
{
    VERSION="0.2"
    SCRIPTNAME=${0##*/}

    dryrun=0	  # Dry-run
    backup_type=0	  # Type of backup
    incr_full_file="" # Full backup file for the incremental
    input_file=""	  # Full backup filename
    output_file=""	  # Output data file
    metadata_file=""  # Output metadata fiole
    pfs_path=""	  # PFS path to be backed up
    backup_dir=""	  # Target directory for backups
    compress=0	  # Compress output file?
    comp_rate=6	  # Compression rate
    verbose=0	  # Verbosity on/off
    list_opt=0	  # List backups
    checksum_opt=0 # Perfom a checksum of all backups
    find_last=0	  # Find last full backup
    timestamp=$(date +'%Y%m%d%H%M%S')
}

info()
{
    [ ${verbose} -eq 1 ] && echo "INFO: $1"
}

#
# err exitval message
#     Display an error and exit
#
err()
{
    exitval=$1
    shift

    echo 1>&2 "$0: ERROR: $*"
    exit $exitval
}

usage()
{
    exitval=${1:-1}
    echo "Usage: ${SCRIPTNAME} [-hlvfk] [-i <full-backup-file|auto>]" \
	"[-c <compress-rate>] -d [<backup-dir>] [pfs path]"
    exit $exitval
}

check_pfs()
{
    info "Validating PFS ${pfs_path}"

    # Backup directory must exist
    if [ -z "${pfs_path}" ]; then
	usage
    fi

    # Make sure we are working on a HAMMER PFS
    hammer pfs-status ${pfs_path} > /dev/null 2>&1
    if [ $? -ne 0 ]; then
	err 2 "${pfs} is not a HAMMER PFS"
    fi
}

get_endtid()
{
    local logfile=$1

    awk -F "[=: ]" -vRS="\r" '{
	if ($4 == "tids") {
		print $6;
		exit
	}
    }' ${logfile}
}

get_uuid()
{
    # Get the shared UUID for the PFS
   hammer pfs-status ${pfs_path} | awk -F'[ =]+' '
	$2 == "shared-uuid" {
		print $3;
		exit;
	}'
}

file2date()
{
    local filename=""
    local filedate=""

    # Extract the date
    filename=$(basename $1)
    filedate=$(echo ${filename} | cut -d "_" -f1)

    date -j -f '%Y%m%d%H%M%S' ${filedate} +"%B %d, %Y %H:%M:%S %Z"
}


update_mdata()
{
    local filename=$(basename ${output_file})
    local uuid=$(get_uuid)
    local endtid=$1
    local md5sum=$(md5 -q ${output_file} 2> /dev/null)

    if [ -z "${endtid}" ]; then
	rm ${output_file}
	err 1 "Couldn't update the metadata file! Deleting ${output_file}"
    fi
    # XXX - Sanity checks missing?!!
    if [ ${dryrun} -eq 0 ]; then
	printf "%s,,,%d,%s,%s,%s\n" ${filename} ${backup_type} ${uuid} \
	    ${endtid} ${md5sum} >> ${metadata_file}
    fi
}

do_backup()
{
    local tmplog=$1
    local compress_opts=""
    local begtid=$2

    # Calculate the compression options
    if [ ${compress} -eq 1 ]; then
	compress_opts=" | xz -c -${comp_rate}"
	output_file="${output_file}.xz"
    fi

    # Generate the datafile according to the options specified
    cmd="hammer -y -v mirror-read ${pfs_path} ${begtid} 2> ${tmplog} \
	${compress_opts} > ${output_file}"

    info "Launching: ${cmd}."
    if [ ${dryrun} -eq 0 ]; then
	# Sync to disk before mirror-read
	hammer synctid ${pfs_path} > /dev/null 2>&1
	eval ${cmd}
	if [ $? -eq 0 ]; then
	    info "Backup completed."
	else
	    rm -f ${output_file}
	    rm -f ${tmplog}
	    err 1 "Failed to created backup data file!"
	fi
    else
	info "Dry-run execution."
    fi
}

full_backup()
{
    local tmplog=$(mktemp)
    local filename=""
    local endtid=""

    # Full backup (no param specified)
    info "Initiating full backup."
    do_backup ${tmplog}

    # Generate the metadata file itself
    metadata_file="${output_file}.bkp"
    endtid=$(get_endtid ${tmplog})

    update_mdata ${endtid}

    # Cleanup
    rm ${tmplog}
}

check_metadata()
{
    local line=""
    local f1=""
    local f2=""

    if [ ! -r ${metadata_file} ]; then
	err 1 "Could not find ${metadata_file}"
    fi

    f1=$(basename ${metadata_file})
    f2=$(head -1 ${metadata_file} | cut -d "," -f1)

    if [ "${f1}" != "${f2}.bkp" ]; then
	err 2 "Bad metadata file ${metadata_file}"
    fi
}

detect_latest_backup()
{
    local latest=""
    local pattern=""

    # XXX
    # Find latest metadata backup file if needed. Right now the timestamp
    # in the filename will let them be sorted by ls. But this could actually
    # change.
    if [ ${find_last} -eq 1 ]; then
	pattern=$(echo ${pfs_path} | tr "/" "_").xz.bkp
	latest=$(ls -1 ${backup_dir}/*${pattern} 2> /dev/null | tail -1)
	if [ -z "${latest}" ]; then
	    err 1 "Failed to detect the latest full backup file."
	fi
	incr_full_file=${latest}
    fi
}

incr_backup()
{
    local tmplog=$(mktemp)
    local begtid=""
    local endtid=""
    local line=""
    local srcuuid=""
    local tgtuuid=""
    local btype=0

    detect_latest_backup

    # Make sure the file exists and it can be read
    if [ ! -r ${incr_full_file} ]; then
	err 1 "Specified file ${incr_full_file} does not exist."
    fi
    metadata_file=${incr_full_file}

    # Verify we were passed a real metadata file
    check_metadata

    # The first backup of the metadata file must be a full one
    line=$(head -1 ${incr_full_file})
    btype=$(echo ${line} | cut -d ',' -f4)
    if [ ${btype} -ne 1 ]; then
	err 1 "No full backup in ${incr_full_file}. Cannot do incremental ones."
    fi

    # Read metadata info for the last backup performed
    line=$(tail -1 ${incr_full_file})
    srcuuid=$(echo $line| cut -d ',' -f 5)
    begtid=$(echo $line| cut -d ',' -f 6)

    # Verify shared uuid are the same
    tgtuuid=$(get_uuid)
    if [ "${srcuuid}" != "${tgtuuid}" ]; then
	err 255 "Shared UUIDs do not match! ${srcuuid} -> ${tgtuuid}"
    fi

    # Do an incremental backup
    info "Initiating incremental backup."
    do_backup ${tmplog} 0x${begtid}

    # Store the metadata in the full backup file
    endtid=$(get_endtid ${tmplog})

    #
    # Handle the case where the hammer mirror-read command did not retrieve
    # any data because the PFS was not modified at all. In that case we keep
    # TID of the previous backup.
    #
    if [ -z "${endtid}" ]; then
	endtid=${begtid}
    fi
    update_mdata ${endtid}

    # Cleanup
    rm ${tmplog}
}

list_backups()
{
    local nofiles=1

    for bkp in ${backup_dir}/*.bkp
    do
	# Skip files that don't exist
	if [ ! -f ${bkp} ]; then
	    continue
	fi
	# Show incremental backups related to the full backup above
	awk -F "," '{
		if ($4 == 1) {
			printf("full: ");
		}
		if ($4 == 2) {
			printf("\tincr: ");
		}
	printf("%s endtid: 0x%s md5: %s\n", $1, $6, $7);
	}' ${bkp}
	nofiles=0
    done

    if [ ${nofiles} -eq 1 ]; then
	err 255 "No backup files found in ${backup_dir}"
    fi

    exit 0
}

checksum_backups()
{
    local nofiles=1
    local storedck=""
    local fileck=""
    local tmp=""

    for bkp in ${backup_dir}/*.bkp
    do
	# Skip files that don't exist
	if [ ! -f ${bkp} ]; then
	    continue
	fi
	# Perform a checksum test
	while read line
	do
	    tmp=$(echo $line | cut -d "," -f1)
	    fname=${backup_dir}/${tmp}
	    storedck=$(echo $line | cut -d "," -f7)
	    fileck=$(md5 -q ${fname} 2> /dev/null)
	    echo -n "${fname} : "
	    if [ ! -f ${fname} ]; then
		echo "MISSING"
		continue
	    elif [ "${storedck}" == "${fileck}" ]; then
		echo "OK"
	    else
		echo "FAILED"
	    fi
	done < ${bkp}
	nofiles=0
    done

    if [ ${nofiles} -eq 1 ]; then
	err 255 "No backup files found in ${backup_dir}"
    fi

    exit 0
}
# -------------------------------------------------------------

# Setup some vars
initialization

# Only can be run by root
if [  $(id -u) -ne 0 ]; then
    err 255 "Only root can run this script."
fi

# Checks hammer program
if [ ! -x /sbin/hammer ]; then
    err 1 'Could not find find hammer(8) program.'
fi

# Handle options
while getopts d:i:c:fvhnlk op
do
    case $op in
	d)
	    backup_dir=$OPTARG
	    ;;
	f)
	    if [ ${backup_type} -eq 2 ]; then
		err 1 "-f and -i are mutually exclusive."
	    fi
	    backup_type=1
	    ;;
	i)
	    if [ ${backup_type} -eq 2 ]; then
		err 1 "-f and -i are mutually exclusive."
	    fi
	    backup_type=2
	    if [ "${OPTARG}" == "auto" ]; then
		find_last=1
	    else
		incr_full_file=$OPTARG
	    fi
	    ;;
	c)
	    compress=1

	    case "$OPTARG" in
		[1-9])
		    comp_rate=$OPTARG
		    ;;
		*)
		    err 1 "Bad compression level specified."
		    ;;
	    esac
	    ;;
	k)
	    checksum_opt=1
	    ;;
	n)
	    dryrun=1
	    ;;
	l)
	    list_opt=1
	    ;;
	v)
	    verbose=1
	    ;;
	h)
	    usage 0
	    ;;
	*)
	    usage
	    ;;
    esac
done

shift $(($OPTIND - 1))

info "hammer-backup version ${VERSION}"

#
# If list option is selected
pfs_path="$1"

# Backup directory must exist
if [ -z "${backup_dir}" ]; then
    usage
elif [ ! -d "${backup_dir}" ]; then
    err 1 "Backup directory does not exist!"
fi
info "Backup dir is ${backup_dir}"

# Output file format is YYYYmmddHHMMSS
tmp=$(echo ${pfs_path} | tr '/' '_')
output_file="${backup_dir}/${timestamp}${tmp}"

# List backups if needed
if [ ${list_opt} -eq 1 ]; then
    info "Listing backups."
    list_backups
fi

# Checksum test
if [ ${checksum_opt} -eq 1 ]; then
    info "Checksum test for all backup files."
    checksum_backups
fi

# Only work on a HAMMER fs
check_pfs

# Actually launch the backup itself
if [ ${backup_type} -eq 1 ]; then
    info "Full backup."
    full_backup
elif [ ${backup_type} -eq 2 ]; then
    info "Incremental backup."
    incr_full_file=${backup_dir}/${incr_full_file}
    incr_backup
else
    err 255 "Impossible backup type."
fi
