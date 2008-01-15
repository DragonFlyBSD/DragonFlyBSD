#!/bin/sh
#
# $DragonFly: src/tools/tools/convert_usb_ids/convert_usb_ids.sh,v 1.2 2008/01/15 12:30:44 matthias Exp $

ub=usbdevs

if [ ! -f $ub ]; then
	echo "Cannot find $ub.  You can get the latest version from FreeBSD"
	echo "http://www.freebsd.org/cgi/cvsweb.cgi/src/sys/dev/usb/usbdevs"
	exit 1
fi

# Function for USB mass storage tables (see umass.c)
umass()
{
	i=0
	while read ip; do
		# Skip over /* ... */ comments
		cc=`echo "$ip" | grep "/\*"`
		if [ -n "$cc" ]; then
			continue
		fi

		# Assume that an umass entry consists of 3 lines
		if [ $i -gt 2 ]; then
			i=0
			continue
		fi

		# The first line
		if [ -n "`echo $ip | grep '{'`" ]; then
			vendor=`echo $ip | awk -F ' ' '{print $2}' | head -1 | \
				tr -d , | sed -e 's/USB_VENDOR_//g'`
			product=`echo $ip | awk -F ' ' '{print $3}'| head -1 | \
				tr -d , | sed -e "s/USB_PRODUCT_${vendor}_//g"`
			release=`echo $ip | awk -F ' ' '{print $4}'| head -1`
			#let i=$i + 1 > /dev/null
		# Second and third line
		else
			# Extract protocol line
			if [ -n "`echo $ip | grep UMASS_PROTO`" ]; then
				proto=`echo $ip | grep UMASS_PROTO`
			fi
			# Extract quirks
			if [ $i -eq 2 ]; then
				quirks=`echo $ip`
			fi
			#let i=$i + 1 > /dev/null
		fi
		if [ -n "$vendor" -a -n "$product" -a -n "$release" ] &&
			[ -n "$proto" -a -n "$quirks" ]; then

			# We use another define
			if [ "$release" = "RID_WILDCARD," ]; then
				release="WILDCARD_ID,"
			fi

			# Get vendor ID from usbdevs
			vendor_id=`grep "vendor $vendor" $ub | head -1 | awk '{print $3}'`

			# Get vendor description from usbdevs
			vendor_desc=`grep "vendor $vendor" $ub | head -1 | awk '{print $4}'`

			# Get product ID from usbdevs
			product_id=`grep "product $vendor $product" $ub | \
				head -1 | awk '{print $4 }'`

			# Get full product name from usbdevs
			product_name=`grep "product $vendor $product" $ub | \
				head -1 | awk '{print $5,$6,$7 }'`

			# Output our version of the device id
			printf "\t"
			echo "/* $vendor_desc $product_name */"
			printf "\t"
			echo -n "{ .vendor = $vendor_id, .product = $product_id, "
			echo ".release = $release"
			printf "\t"
			echo "  .proto  = $proto"
			printf "\t"
			echo "  .quirks = $quirks"
			printf "\t"
			echo "},"
			release=
			vendor=
			product=
			quirks=
			i=10
		fi
		let i=$i + 1 > /dev/null
	done
}

# Function for non-umass devices
nonumass()
{
	while read ip; do
		# Skip lines not beginning with {
		if [ -z "`echo $ip | grep '{ '`" ]; then
			continue
		fi

		# Get vendor
		vendor=`echo $ip | awk -F ' ' '{print $2}' | head -1 | \
			tr -d , | sed -e 's/USB_VENDOR_//g'`

		# Get product
		product=`echo $ip | awk -F ' ' '{print $3}'| head -1 | \
			tr -d , | sed -e "s/USB_PRODUCT_${vendor}_//g"`

		# Get possible flag (eg PALM4)
		dflag=`echo $ip | awk -F ' ' '{print $5}'`

		# Get vendor ID from usbdevs
		vendor_id=`grep "vendor $vendor" $ub | head -1 | awk '{print $3}'`

		# Get vendor description from usbdevs
		vendor_desc=`grep "vendor $vendor" $ub | head -1 | awk '{print $4}'`

		# Get product ID from ubsdevs
		product_id=`grep "product $vendor $product" $ub | \
			head -1 | awk '{print $4 }'`

		# Get full name of the product from usbdevs
		product_name=`grep "product $vendor $product" $ub | \
			head -1 | awk '{print $5,$6,$7 }'`

		if [ -n "$vendor_id" -a -n "$vendor_desc" ] &&
				[ -n "$product_id" -a -n "$product_name" ]; then
			# More complex entry with a flag
			if [ "$dflag" != "" ]; then
				printf "\t"
				echo "{{ USB_DEVICE($vendor_id, $product_id) }, $dflag }, /* $vendor_desc $product_name */"
			# Entry without a flag
			elif [ "$dflag" = "" ]; then
				printf "\t"
				echo "{ USB_DEVICE($vendor_id, $product_id) }, /* $vendor_desc $product_name */"
			fi
		fi
	done
}

DFLAG=0
case "$1" in
	"-d")
		DFLAG=1; break;;
esac

if [ $DFLAG -eq 1 ]; then
	umass
else
	nonumass
fi

exit $?
