# ESP32 sensors → Yandex Monitoring

The firmware reads SCD40 and either BMP580 or BME280 and sends sensor metrics to Yandex Monitoring.

## Flash layout (no OTA)

The project uses a single `factory` application partition, no OTA slots and no OTA metadata partition. NVS is retained for Wi-Fi and device settings, as is the PHY initialization partition. For a new configuration, `sdkconfig.defaults` selects 4 MiB flash and `partitions/4mb.csv`.

| Flash capacity | Custom partition CSV | Application size |
| --- | --- | ---: |
| 2 MiB | `partitions/2mb.csv` | 1984 KiB |
| 4 MiB | `partitions/4mb.csv` | 4032 KiB |
| 8 MiB | `partitions/8mb.csv` | 8128 KiB |
| 16 MiB | `partitions/16mb.csv` | 16320 KiB |

If the board has a different flash capacity, or you already have a local `sdkconfig`, use `idf.py menuconfig`:

1. Set **Serial flasher config → Flash size** to the actual capacity of the board.
2. Under **Partition Table**, select **Custom partition table CSV** and the corresponding file from the table above. Use the default partition table offset, `0x8000`.
3. Build and use `idf.py flash` for the first update, so the partition table and bootloader are flashed as well as the application. Do not use `app-flash` alone when changing the layout.

Changing a partition layout can affect previously stored flash data. Normally only the firmware needs updating after the new layout is installed; **do not erase the entire flash** unless you also want to clear the stored device labels (and other NVS settings).

## Build-time settings

Copy `main/credentials.h.example` to `main/credentials.h` and set the Wi-Fi credentials and Monitoring token locally. Configure the Monitoring folder ID, pressure sensor and GPIOs as before. The `location` and `room` settings are no longer part of the build configuration.

For a local defaults file, use `sdkconfig.defaults.local` (ignored by Git), e.g. `idf.py -D SDKCONFIG_DEFAULTS='sdkconfig.defaults;sdkconfig.defaults.local' build`.

## Local status and configuration

Connect to the same Wi-Fi network as the device. Open `http://<esp32-ip>/` (the IP address is printed in the serial log after connecting). The page shows the most recent valid measurement values and their readiness flags, the SCD40's new-data flag from the latest polling cycle, uptime, and the stored `location` and `room` labels. Measurements refresh automatically every five seconds; typing into the form is not interrupted by refreshes.

Fill in both labels and click **Save**. On first installation of this firmware, previously compiled-in labels are not migrated; enter them on the page once. The firmware commits the pair to NVS as a single record and uses it for the next monitoring submission, without restarting. Both labels are required; an empty or whitespace-only value disables monitoring submissions, but measurement, the local page and the watchdog continue to operate. Values survive normal resets and firmware updates that preserve NVS.

Labels can contain up to 128 UTF-8 bytes each. Leading/trailing ASCII whitespace is trimmed.

The HTTP page is intended for a trusted local network; it has no authentication or TLS. Do not expose the device HTTP port to the Internet or an untrusted network.

### Local HTTP API

- `GET /api/status`: current labels, measurements (`value` and `ready`), SCD40 data readiness, uptime and whether labels permit sending.
- `POST /api/labels`: set both labels with `Content-Type: application/json`, e.g. `{"location":"home","room":"living_room"}`. Returns the saved values or an HTTP error if validation or NVS saving fails. An empty string is accepted to disable submission.
