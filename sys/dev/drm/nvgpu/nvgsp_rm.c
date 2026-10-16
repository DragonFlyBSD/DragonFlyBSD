/*-
 * SPDX-License-Identifier: BSD-2-Clause
 *
 * GSP RM object boundary for the native NVIDIA GPU driver.
 */

#include "nvgsp_rm.h"
#include "nvgsp_priv.h"

#define NV_VGPU_MSG_FUNCTION_FREE		10
#define NV_VGPU_MSG_FUNCTION_GSP_RM_CONTROL	76
#define NV_VGPU_MSG_FUNCTION_GSP_RM_ALLOC	103

#define NV01_ROOT				0x00000000u
#define NV01_DEVICE_0				0x00000080u
#define NV20_SUBDEVICE_0			0x00002080u
#define FERMI_VASPACE_A			0x000090f1u
#define TURING_USERMODE_A			0x0000c461u
#define NVGSP_RM_DEVICE			0xde1d0000u
#define NVGSP_RM_SUBDEVICE			0x5d1d0000u
#define NVGSP_RM_VASPACE			0x90f10000u
#define NVGSP_RM_USERMODE			0xc4610000u

struct nvgsp_rm_alloc_rpc {
	uint32_t hClient;
	uint32_t hParent;
	uint32_t hObject;
	uint32_t hClass;
	uint32_t status;
	uint32_t paramsSize;
	uint32_t flags;
	uint8_t reserved[4];
	uint8_t params[];
};

struct nvgsp_rm_control_rpc {
	uint32_t hClient;
	uint32_t hObject;
	uint32_t cmd;
	uint32_t status;
	uint32_t paramsSize;
	uint32_t flags;
	uint8_t params[];
};

struct nvgsp_rm_free_rpc {
	struct {
		uint32_t hRoot;
		uint32_t hObjectParent;
		uint32_t hObjectOld;
		int32_t status;
	} params;
};

struct nvgsp_root_alloc_params {
	uint32_t hClient;
	uint32_t processID;
	char processName[100];
	uint8_t pad_to_align8[4];
	uint64_t pOsPidInfo;
};

struct nvgsp_device_alloc_params {
	uint32_t deviceId;
	uint32_t hClientShare;
	uint32_t hTargetClient;
	uint32_t hTargetDevice;
	int32_t flags;
	uint64_t vaSpaceSize;
	uint64_t vaStartInternal;
	uint64_t vaLimitInternal;
	int32_t vaMode;
};

struct nvgsp_subdevice_alloc_params {
	uint32_t subDeviceId;
};

struct nvgsp_vaspace_alloc_params {
	uint32_t index;
	uint32_t flags;
	uint64_t vaBase;
	uint64_t vaSize;
	uint64_t bigPageSize;
};

#define NV_VASPACE_ALLOCATION_INDEX_GPU_NEW	0u

static uint32_t
nvgsp_rm_get_client_child_handle(struct nvgsp_client *client, uint32_t base)
{
	return (base | (client->object.handle & 0x00000fffu));
}

void *
nvgsp_rm_get_alloc(struct nvgsp_object *parent, uint32_t handle,
    uint32_t oclass, uint32_t params_size, struct nvgsp_object *new_obj)
{
	struct nvgsp_client *client;
	struct nvgsp_rm_alloc_rpc *rpc;

	if (parent == NULL || new_obj == NULL) {
		nvgpu_log(NVGPU_LOG_INFO,
		    "rm alloc invalid object parent=%p new=%p handle=0x%x class=0x%x size=%u\n",
		    parent, new_obj, handle, oclass, params_size);
		return (NULL);
	}
	client = parent->client;
	if (client == NULL || client->gsp == NULL) {
		nvgpu_log(NVGPU_LOG_INFO,
		    "rm alloc invalid parent=%p client=%p handle=0x%x class=0x%x size=%u\n",
		    parent, client, handle, oclass, params_size);
		return (NULL);
	}

	rpc = nvgsp_rpc_get(client->gsp, NV_VGPU_MSG_FUNCTION_GSP_RM_ALLOC,
	    sizeof(*rpc) + params_size);
	if (rpc == NULL) {
		nvgpu_log(NVGPU_LOG_INFO,
		    "rm alloc rpc_get failed parent=%p client=%p gsp=%p handle=0x%x class=0x%x size=%u\n",
		    parent, client, client->gsp, handle, oclass, params_size);
		return (NULL);
	}

	new_obj->client = client;
	new_obj->parent = parent;
	new_obj->handle = handle;
	nvgpu_log(NVGPU_LOG_DEBUG,
	    "rm alloc get parent=%p client=%p gsp=%p rpc=%p params=%p hclient=0x%x hparent=0x%x hobject=0x%x class=0x%x size=%u\n",
	    parent, client, client->gsp, rpc, rpc->params, client->object.handle,
	    parent->handle, handle, oclass, params_size);

	rpc->hClient = client->object.handle;
	rpc->hParent = parent->handle;
	rpc->hObject = handle;
	rpc->hClass = oclass;
	rpc->status = 0;
	rpc->paramsSize = params_size;
	rpc->flags = 0;
	return (rpc->params);
}

static struct nvgsp_rm_alloc_rpc *
nvgsp_rm_get_alloc_hdr(void *params)
{
	return ((struct nvgsp_rm_alloc_rpc *)((uint8_t *)params -
	    offsetof(struct nvgsp_rm_alloc_rpc, params)));
}

static struct nvgsp_rm_control_rpc *
nvgsp_rm_get_ctrl_hdr(void *params)
{
	return ((struct nvgsp_rm_control_rpc *)((uint8_t *)params -
	    offsetof(struct nvgsp_rm_control_rpc, params)));
}

int
nvgsp_rm_write_alloc(struct nvgsp_object *obj, void *params)
{
	struct nvgsp_state *gsp = obj->client->gsp;
	struct nvgsp_rm_alloc_rpc *rpc = nvgsp_rm_get_alloc_hdr(params);
	struct nvgsp_rm_alloc_rpc *rep;
	uint32_t hclass = rpc->hClass;
	uint32_t hobject = rpc->hObject;
	uint32_t hparent = rpc->hParent;
	uint32_t params_size = rpc->paramsSize;
	int error = 0;

	rep = nvgsp_rpc_push(gsp, rpc, NVGSP_RPC_REPLY_RECV,
	    sizeof(*rpc) + params_size);
	if (rep == NULL)
		return (EIO);
	nvgpu_log(NVGPU_LOG_DEBUG,
	    "rm alloc reply cls=0x%x obj=0x%x parent=0x%x status=0x%x rep=%p\n",
	    hclass, hobject, hparent, rep->status, rep);
	if (rep->status != 0) {
		nvgpu_log(NVGPU_LOG_INFO,
		    "rm alloc cls=0x%x obj=0x%x parent=0x%x failed status=0x%x\n",
		    hclass, hobject, hparent, rep->status);
		error = EIO;
	}
	nvgsp_rpc_complete(gsp, rep);
	nvgpu_log(NVGPU_LOG_DEBUG,
	    "rm alloc done cls=0x%x obj=0x%x parent=0x%x error=%d\n",
	    hclass, hobject, hparent, error);
	return (error);
}

int
nvgsp_rm_read_alloc(struct nvgsp_object *obj, void **params, uint32_t repc)
{
	struct nvgsp_state *gsp = obj->client->gsp;
	struct nvgsp_rm_alloc_rpc *rpc = nvgsp_rm_get_alloc_hdr(*params);
	struct nvgsp_rm_alloc_rpc *rep;
	int error = 0;

	rep = nvgsp_rpc_push(gsp, rpc, NVGSP_RPC_REPLY_RECV, sizeof(*rpc) + repc);
	if (rep == NULL) {
		*params = NULL;
		return (EIO);
	}
	if (rep->status != 0)
		error = EIO;
	if (repc != 0)
		*params = rep->params;
	else {
		nvgsp_rpc_complete(gsp, rep);
		*params = NULL;
	}
	return (error);
}

void
nvgsp_rm_complete_alloc(struct nvgsp_object *obj, void *params)
{
	nvgsp_rpc_complete(obj->client->gsp, nvgsp_rm_get_alloc_hdr(params));
}

int
nvgsp_rm_free(struct nvgsp_object *obj)
{
	struct nvgsp_rm_free_rpc *rpc;

	if (obj == NULL || obj->client == NULL || obj->handle == 0)
		return (0);
	rpc = nvgsp_rpc_get(obj->client->gsp, NV_VGPU_MSG_FUNCTION_FREE,
	    sizeof(*rpc));
	if (rpc == NULL)
		return (ENOMEM);
	rpc->params.hRoot = obj->client->object.handle;
	rpc->params.hObjectParent = 0;
	rpc->params.hObjectOld = obj->handle;
	rpc->params.status = 0;
	return (nvgsp_rpc_wr(obj->client->gsp, rpc, NVGSP_RPC_REPLY_RECV));
}

void *
nvgsp_rm_get_ctrl(struct nvgsp_object *obj, uint32_t cmd, uint32_t params_size)
{
	struct nvgsp_rm_control_rpc *rpc;

	rpc = nvgsp_rpc_get(obj->client->gsp, NV_VGPU_MSG_FUNCTION_GSP_RM_CONTROL,
	    sizeof(*rpc) + params_size);
	if (rpc == NULL)
		return (NULL);
	rpc->hClient = obj->client->object.handle;
	rpc->hObject = obj->handle;
	rpc->cmd = cmd;
	rpc->status = 0;
	rpc->paramsSize = params_size;
	rpc->flags = 0;
	return (rpc->params);
}

int
nvgsp_rm_read_ctrl(struct nvgsp_object *obj, void **params, uint32_t repc)
{
	struct nvgsp_state *gsp = obj->client->gsp;
	struct nvgsp_rm_control_rpc *rpc = nvgsp_rm_get_ctrl_hdr(*params);
	struct nvgsp_rm_control_rpc *rep;
	uint32_t hclient = rpc->hClient;
	uint32_t hobject = rpc->hObject;
	uint32_t cmd = rpc->cmd;
	uint32_t params_size = rpc->paramsSize;
	int error = 0;

	nvgpu_log(NVGPU_LOG_DEBUG,
	    "rm ctrl push cmd=0x%x obj=0x%x client=0x%x params=%u repc=%u\n",
	    cmd, hobject, hclient, params_size, repc);
	rep = nvgsp_rpc_push(gsp, rpc, NVGSP_RPC_REPLY_RECV, sizeof(*rpc) + repc);
	if (rep == NULL) {
		nvgpu_log(NVGPU_LOG_INFO,
		    "rm ctrl cmd=0x%x obj=0x%x client=0x%x failed: no reply\n",
		    cmd, hobject, hclient);
		*params = NULL;
		return (EIO);
	}
	nvgpu_log(NVGPU_LOG_DEBUG,
	    "rm ctrl reply cmd=0x%x obj=0x%x client=0x%x status=0x%x rep=%p\n",
	    cmd, hobject, hclient, rep->status, rep);
	if (rep->status != 0) {
		nvgpu_log(NVGPU_LOG_INFO,
		    "rm ctrl cmd=0x%x obj=0x%x client=0x%x failed status=0x%x\n",
		    cmd, hobject, hclient, rep->status);
		error = EIO;
	}
	if (repc != 0)
		*params = rep->params;
	else {
		nvgsp_rpc_complete(gsp, rep);
		*params = NULL;
	}
	return (error);
}

int
nvgsp_rm_write_ctrl(struct nvgsp_object *obj, void *params)
{
	void *reply = params;
	return (nvgsp_rm_read_ctrl(obj, &reply, 0));
}

void
nvgsp_rm_complete_ctrl(struct nvgsp_object *obj, void *params)
{
	nvgsp_rpc_complete(obj->client->gsp, nvgsp_rm_get_ctrl_hdr(params));
}

int
nvgsp_rm_construct_client(struct nvgsp_state *gsp, uint32_t handle, struct nvgsp_client *client)
{
	struct nvgsp_root_alloc_params *args;
	int error;

	memset(client, 0, sizeof(*client));
	client->gsp = gsp;
	client->object.client = client;
	client->object.handle = handle;

	args = nvgsp_rm_get_alloc(&client->object, handle, NV01_ROOT,
	    sizeof(*args), &client->object);
	if (args == NULL)
		return (ENOMEM);
	args->hClient = handle;
	args->processID = (uint32_t)~0u;
	strncpy(args->processName, "dfly-nvgpu", sizeof(args->processName));
	args->pOsPidInfo = 0;
	error = nvgsp_rm_write_alloc(&client->object, args);
	if (error != 0)
		memset(client, 0, sizeof(*client));
	return (error);
}

int
nvgsp_rm_destroy_client(struct nvgsp_client *client)
{
	return (nvgsp_rm_free(&client->object));
}

int
nvgsp_rm_construct_device(struct nvgsp_client *client, struct nvgsp_device *device)
{
	struct nvgsp_device_alloc_params *dargs;
	struct nvgsp_subdevice_alloc_params *sargs;
	int error;

	memset(device, 0, sizeof(*device));
	dargs = nvgsp_rm_get_alloc(&client->object,
	    nvgsp_rm_get_client_child_handle(client, NVGSP_RM_DEVICE), NV01_DEVICE_0,
	    sizeof(*dargs), &device->object);
	if (dargs == NULL)
		return (ENOMEM);
	dargs->hClientShare = client->object.handle;
	error = nvgsp_rm_write_alloc(&device->object, dargs);
	if (error != 0)
		return (error);

	sargs = nvgsp_rm_get_alloc(&device->object,
	    nvgsp_rm_get_client_child_handle(client, NVGSP_RM_SUBDEVICE), NV20_SUBDEVICE_0,
	    sizeof(*sargs), &device->subdevice);
	if (sargs == NULL) {
		nvgsp_rm_free(&device->object);
		return (ENOMEM);
	}
	sargs->subDeviceId = 0;
	error = nvgsp_rm_write_alloc(&device->subdevice, sargs);
	if (error != 0) {
		nvgsp_rm_free(&device->object);
		return (error);
	}
	return (0);
}

int
nvgsp_rm_destroy_device(struct nvgsp_device *device)
{
	nvgsp_rm_free(&device->subdevice);
	nvgsp_rm_free(&device->object);
	return (0);
}

int
nvgsp_rm_construct_vaspace(struct nvgsp_device *device, struct nvgsp_object *vaspace)
{
	struct nvgsp_vaspace_alloc_params *args;
	int error;

	memset(vaspace, 0, sizeof(*vaspace));
	args = nvgsp_rm_get_alloc(&device->object,
	    nvgsp_rm_get_client_child_handle(device->object.client, NVGSP_RM_VASPACE),
	    FERMI_VASPACE_A, sizeof(*args), vaspace);
	if (args == NULL)
		return (ENOMEM);
	args->index = NV_VASPACE_ALLOCATION_INDEX_GPU_NEW;
	error = nvgsp_rm_write_alloc(vaspace, args);
	if (error != 0)
		memset(vaspace, 0, sizeof(*vaspace));
	return (error);
}

int
nvgsp_rm_free_graphics_object(struct nvgpu_device *gpu)
{
	(void)gpu;
	return (0);
}
