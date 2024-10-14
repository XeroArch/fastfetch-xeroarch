#include "physicaldisk.h"
#include "common/io/io.h"
#include "common/properties.h"
#include "util/stringUtils.h"

#include <ctype.h>
#include <limits.h>
#include <unistd.h>
#include <fcntl.h>

static double detectNvmeTemp(int devfd)
{
    char pathHwmon[] = "hwmon$/temp1_input";

    for (char c = '0'; c <= '9'; c++) // hopefully there's only one digit
    {
        pathHwmon[strlen("hwmon")] = c;
        char buffer[64];
        ssize_t size = ffReadFileDataRelative(devfd, pathHwmon, sizeof(buffer), buffer);
        if (size > 0)
        {
            buffer[size] = '\0';
            double temp = strtod(buffer, NULL);
            return temp > 0 ? temp / 1000 : FF_PHYSICALDISK_TEMP_UNSET;
        }
    }

    return FF_PHYSICALDISK_TEMP_UNSET;
}

static void parsePhysicalDisk(int dfd, const char* devName, const char* pathSysDeviceReal, FFPhysicalDiskOptions* options, FFlist* result)
{
    int devfd = openat(dfd, "device", O_RDONLY | O_CLOEXEC);
    if (devfd < 0) return;

    FF_STRBUF_AUTO_DESTROY name = ffStrbufCreate();

    {
        if (ffAppendFileBufferRelative(devfd, "vendor", &name))
        {
            ffStrbufTrimRightSpace(&name);
            if (name.length > 0)
                ffStrbufAppendC(&name, ' ');
        }

        ffAppendFileBufferRelative(devfd, "model", &name);
        ffStrbufTrimRightSpace(&name);

        if (name.length == 0)
            ffStrbufSetS(&name, devName);

        if (ffStrStartsWith(devName, "nvme"))
        {
            int devid, nsid;
            if (sscanf(devName, "nvme%dn%d", &devid, &nsid) == 2)
            {
                bool multiNs = nsid > 1;
                if (!multiNs)
                {
                    char pathSysBlock[32];
                    snprintf(pathSysBlock, sizeof(pathSysBlock), "/dev/nvme%dn2", devid);
                    multiNs = access(pathSysBlock, F_OK) == 0;
                }
                if (multiNs)
                {
                    // In Asahi Linux, there are multiple namespaces for the same NVMe drive.
                    ffStrbufAppendF(&name, " - %d", nsid);
                }
            }
        }

        if (options->namePrefix.length && !ffStrbufStartsWith(&name, &options->namePrefix))
            return;
    }

    FFPhysicalDiskResult* device = (FFPhysicalDiskResult*) ffListAdd(result);
    device->type = FF_PHYSICALDISK_TYPE_NONE;
    ffStrbufInitMove(&device->name, &name);
    ffStrbufInitF(&device->devPath, "/dev/%s", devName);

    {
        ffStrbufInit(&device->interconnect);
        if (strstr(pathSysDeviceReal, "/usb") != NULL)
            ffStrbufSetS(&device->interconnect, "USB");
        else if (strstr(pathSysDeviceReal, "/nvme") != NULL)
            ffStrbufSetS(&device->interconnect, "NVMe");
        else if (strstr(pathSysDeviceReal, "/ata") != NULL)
            ffStrbufSetS(&device->interconnect, "ATA");
        else if (strstr(pathSysDeviceReal, "/scsi") != NULL)
            ffStrbufSetS(&device->interconnect, "SCSI");
        else
        {
            if (ffAppendFileBufferRelative(devfd, "transport", &device->interconnect))
                ffStrbufTrimRightSpace(&device->interconnect);
        }
    }

    {
        char isRotationalChar = '1';
        if (ffReadFileDataRelative(dfd, "queue/rotational", 1, &isRotationalChar) > 0)
            device->type |= isRotationalChar == '1' ? FF_PHYSICALDISK_TYPE_HDD : FF_PHYSICALDISK_TYPE_SSD;
    }

    {
        char blkSize[32];
        ssize_t fileSize = ffReadFileDataRelative(dfd, "size", sizeof(blkSize) - 1, blkSize);
        if (fileSize > 0)
        {
            blkSize[fileSize] = 0;
            device->size = (uint64_t) strtoul(blkSize, NULL, 10) * 512;
        }
        else
            device->size = 0;
    }

    {
        char removableChar = '0';
        if (ffReadFileDataRelative(dfd, "removable", 1, &removableChar) > 0)
            device->type |= removableChar == '1' ? FF_PHYSICALDISK_TYPE_REMOVABLE : FF_PHYSICALDISK_TYPE_FIXED;
    }

    {
        char roChar = '0';
        if (ffReadFileDataRelative(dfd, "ro", 1, &roChar) > 0)
            device->type |= roChar == '1' ? FF_PHYSICALDISK_TYPE_READONLY : FF_PHYSICALDISK_TYPE_READWRITE;
    }

    {
        ffStrbufInit(&device->serial);
        if (ffReadFileBufferRelative(devfd, "serial", &device->serial))
            ffStrbufTrimRightSpace(&device->serial);
    }

    {
        ffStrbufInit(&device->revision);
        if (ffReadFileBufferRelative(devfd, "firmware_rev", &device->revision))
            ffStrbufTrimRightSpace(&device->revision);
        else
        {
            if (ffReadFileBufferRelative(devfd, "rev", &device->revision))
                ffStrbufTrimRightSpace(&device->revision);
        }
    }

    if (options->temp)
        device->temperature = detectNvmeTemp(devfd);
    else
        device->temperature = FF_PHYSICALDISK_TEMP_UNSET;
}

const char* ffDetectPhysicalDisk(FFlist* result, FFPhysicalDiskOptions* options)
{
    FF_AUTO_CLOSE_DIR DIR* sysBlockDirp = opendir("/sys/block/");
    if(sysBlockDirp == NULL)
        return "opendir(\"/sys/block/\") == NULL";

    struct dirent* sysBlockEntry;
    while ((sysBlockEntry = readdir(sysBlockDirp)) != NULL)
    {
        const char* const devName = sysBlockEntry->d_name;

        if (devName[0] == '.')
            continue;

        char pathSysBlock[sizeof("/sys/block/") + sizeof(sysBlockEntry->d_name)];
        snprintf(pathSysBlock, sizeof(pathSysBlock), "/sys/block/%s", devName);

        char pathSysDeviceReal[PATH_MAX];
        ssize_t pathLength = readlink(pathSysBlock, pathSysDeviceReal, sizeof(pathSysDeviceReal) - 1);
        if (pathLength < 0)
            continue;
        pathSysDeviceReal[pathLength] = '\0';

        if (strstr(pathSysDeviceReal, "/virtual/")) // virtual device
            continue;

        int dfd = openat(dirfd(sysBlockDirp), devName, O_RDONLY | O_CLOEXEC);
        if (dfd > 0) parsePhysicalDisk(dfd, devName, pathSysDeviceReal, options, result);
    }

    return NULL;
}
