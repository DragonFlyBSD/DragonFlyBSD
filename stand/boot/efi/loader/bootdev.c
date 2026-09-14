/*
 * EFI boot filesystem selection.  The firmware source is kept separate
 * from the filesystem selected for loading the kernel.
 */
#include <sys/stat.h>
#include <stand.h>
#include <bootstrap.h>
#include <efi.h>
#include <efilib.h>
#include "loader_efi.h"

struct boot_candidate {
	char device[32];
	const char *directory;
	struct fs_ops *filesystem;
};

static int gpt_partition(EFI_HANDLE);
static int probe_device(const char *, struct boot_candidate *);
static int candidate_marker(const struct boot_candidate *);

/* Return one for the new entry, zero for the unchanged legacy entry. */
int
efi_bootdev_entry(EFI_HANDLE source)
{
	EFI_GUID esp = { 0xc12a7328, 0xf81f, 0x11d2,
	    { 0xba, 0x4b, 0x00, 0xa0, 0xc9, 0x3e, 0xc9, 0x3b } };
	EFI_DEVICE_PATH *node;
	EFI_STATUS status;
	void *interface;
	EFI_GUID path_guid = DEVICE_PATH_PROTOCOL;

	status = BS->HandleProtocol(source, &esp, &interface);
	if (status == EFI_SUCCESS)
		return (1);
	if (status != EFI_UNSUPPORTED)
		return (-efi_status_to_errno(status));
	status = OpenProtocolByHandle(source, &path_guid, (void **)&node);
	if (EFI_ERROR(status))
		return (-efi_status_to_errno(status));
	for (; !IsDevicePathEnd(node); node = NextDevicePathNode(node)) {
		if (DevicePathNodeLength(node) < sizeof(*node))
			return (-EINVAL);
		if (DevicePathType(node) == MEDIA_DEVICE_PATH &&
		    DevicePathSubType(node) == MEDIA_CDROM_DP)
			return (1);
	}
	return (0);
}

static int
gpt_partition(EFI_HANDLE handle)
{
	EFI_DEVICE_PATH *node;
	HARDDRIVE_DEVICE_PATH *partition;

	EFI_GUID path_guid = DEVICE_PATH_PROTOCOL;

	if (EFI_ERROR(OpenProtocolByHandle(handle, &path_guid,
	    (void **)&node)))
		return (0);
	for (; !IsDevicePathEnd(node); node = NextDevicePathNode(node)) {
		if (DevicePathNodeLength(node) < sizeof(*node))
			return (0);
		if (DevicePathType(node) != MEDIA_DEVICE_PATH ||
		    DevicePathSubType(node) != MEDIA_HARDDRIVE_DP)
			continue;
		if (DevicePathNodeLength(node) < sizeof(*partition))
			return (0);
		partition = (void *)node;
		return (partition->MBRType == MBR_TYPE_EFI_PARTITION_TABLE_HEADER);
	}
	return (0);
}

static int
probe_device(const char *name, struct boot_candidate *candidate)
{
	struct efi_devdesc *device;
	struct stat st;
	char path[64];
	int error, fd;

	error = efi_parsedev(&device, name, NULL);
	if (error != 0)
		return (error);
	if (snprintf(candidate->device, sizeof(candidate->device), "%s",
	    efi_fmtdev(device)) >= (int)sizeof(candidate->device)) {
		free(device);
		return (ENAMETOOLONG);
	}
	snprintf(path, sizeof(path), "%s/", candidate->device);
	fd = open(path, O_RDONLY);
	if (fd < 0) {
		error = errno;
		free(device);
		return (error);
	}
	candidate->filesystem = files[fd].f_ops;
	error = fstat(fd, &st) == 0 ? 0 : errno;
	if (close(fd) != 0 && error == 0)
		error = errno;
	if (error == 0 && !S_ISDIR(st.st_mode))
		error = ENOTDIR;
	if (error == 0 && candidate->filesystem == &hammer2_fsops &&
	    (device->d_dev != &efipart_dev ||
	    !gpt_partition(efi_find_handle(device->d_dev,
	    device->d_kind.efidisk.unit))))
		error = ENXIO;
	free(device);
	if (error != 0)
		return (error);

	candidate->directory = "/";
	if (candidate->filesystem == &hammer2_fsops)
		return (0);
	snprintf(path, sizeof(path), "%s/boot", candidate->device);
	if (stat(path, &st) == 0 && S_ISDIR(st.st_mode))
		candidate->directory = "/boot/";
	else if (candidate->filesystem == &cd9660_fsops)
		return (ENOENT);
	return (0);
}

static int
candidate_marker(const struct boot_candidate *candidate)
{
	struct stat st;
	char path[80];

	snprintf(path, sizeof(path), "%s%sdloader.rc", candidate->device,
	    candidate->directory);
	if (stat(path, &st) == 0 && S_ISREG(st.st_mode))
		return (1);
	snprintf(path, sizeof(path), "%s/boot.config", candidate->device);
	if (stat(path, &st) == 0)
		return (S_ISREG(st.st_mode));
	if (errno != ENOENT)
		return (0);
	snprintf(path, sizeof(path), "%s/boot/config", candidate->device);
	return (stat(path, &st) == 0 && S_ISREG(st.st_mode));
}

/* Use the existing devices, then read boot1's optional arguments once. */
int
efi_bootdev_select(EFI_HANDLE source, char **device, const char **directory)
{
	struct boot_candidate selected, candidate, fallback;
	struct devsw *source_device;
	EFI_GUID path_guid = DEVICE_PATH_PROTOCOL;
	EFI_DEVICE_PATH *source_path = NULL, *path;
	EFI_HANDLE handle;
	CHAR16 **argv, *storage;
	struct stat st;
	const char *target;
	char name[80], *buffer;
	size_t used;
	ssize_t bytes;
	int source_unit, unit, pass, group, have_fallback;
	int fd, error, argc;

	target = getenv("currdev");
	if (target != NULL) {
		error = probe_device(target, &selected);
		if (error != 0)
			return (error);
		goto config;
	}

	source_unit = -1;
	if (efi_handle_lookup(source, &source_device, &unit, NULL) == 0 &&
	    source_device == &efipart_dev)
		source_unit = unit;
	if (EFI_ERROR(OpenProtocolByHandle(source, &path_guid,
	    (void **)&source_path)))
		source_path = NULL;
	have_fallback = 0;
	for (pass = 0; pass < 3; pass++) {
		for (unit = 0; (handle = efi_find_handle(&efipart_dev,
		    unit)) != NULL; unit++) {
			group = unit == source_unit ? 0 : 2;
			if (group != 0 && source_path != NULL &&
			    !EFI_ERROR(OpenProtocolByHandle(handle, &path_guid,
			    (void **)&path)) &&
			    efi_device_paths_match(source_path, path))
				group = 1;
			if (group != pass)
				continue;
			snprintf(name, sizeof(name), "part%d:", unit);
			if (probe_device(name, &candidate) != 0)
				continue;
			/* Only automatic selection limits the filesystem types. */
			if (candidate.filesystem != &hammer2_fsops &&
			    candidate.filesystem != &ufs_fsops &&
			    candidate.filesystem != &cd9660_fsops)
				continue;
			if (candidate_marker(&candidate)) {
				selected = candidate;
				goto config;
			}
			if (!have_fallback) {
				fallback = candidate;
				have_fallback = 1;
			}
		}
	}
	if (!have_fallback)
		return (ENOENT);
	selected = fallback;

config:
	snprintf(name, sizeof(name), "%s/boot.config", selected.device);
	fd = open(name, O_RDONLY);
	if (fd < 0 && errno == ENOENT) {
		snprintf(name, sizeof(name), "%s/boot/config", selected.device);
		fd = open(name, O_RDONLY);
	}
	/* As in boot1, an unreadable optional configuration is ignored. */
	if (fd < 0)
		goto done;
	buffer = NULL;
	error = fstat(fd, &st) == 0 ? 0 : errno;
	if (error == 0 && (st.st_size < 0 ||
	    (uintmax_t)st.st_size >= (size_t)-1))
		error = EFBIG;
	if (error == 0) {
		buffer = malloc(st.st_size + 1);
		if (buffer == NULL)
			error = ENOMEM;
	}
	used = 0;
	while (error == 0 && used < (size_t)st.st_size) {
		bytes = read(fd, buffer + used, st.st_size - used);
		if (bytes <= 0) {
			error = bytes < 0 ? errno : EIO;
			break;
		}
		used += bytes;
	}
	if (close(fd) != 0 && error == 0)
		error = errno;
	if (error != 0) {
		free(buffer);
		goto done;
	}
	if (memchr(buffer, '\0', used) != NULL) {
		free(buffer);
		return (EINVAL);
	}
	buffer[used] = '\0';
	error = efi_get_args(buffer, used + 1, (CHAR16 *)L"loader.efi",
	    &argc, &argv, &storage);
	if (error == 0) {
		efi_apply_args(argc, argv);
		free(argv);
		free(storage);
	}
	free(buffer);
	if (error != 0)
		return (error);
	target = getenv("currdev");
	if (target != NULL && (error = probe_device(target, &selected)) != 0)
		return (error);
done:
	*device = strdup(selected.device);
	if (*device == NULL)
		return (ENOMEM);
	*directory = selected.directory;
	return (0);
}
