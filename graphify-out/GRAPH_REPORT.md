# Graph Report - .  (2026-05-05)

## Corpus Check
- cluster-only mode — file stats not available

## Summary
- 1664 nodes · 4551 edges · 103 communities (92 shown, 11 thin omitted)
- Extraction: 90% EXTRACTED · 10% INFERRED · 0% AMBIGUOUS · INFERRED: 463 edges (avg confidence: 0.8)
- Token cost: 0 input · 0 output

## Graph Freshness
- Built from commit: `76ae536a`
- Run `git rev-parse HEAD` and compare to check if the graph is stale.
- Run `graphify update .` after code changes (no API cost).

## Community Hubs (Navigation)
- [[_COMMUNITY_Community 0|Community 0]]
- [[_COMMUNITY_Community 1|Community 1]]
- [[_COMMUNITY_Community 2|Community 2]]
- [[_COMMUNITY_Community 3|Community 3]]
- [[_COMMUNITY_Community 4|Community 4]]
- [[_COMMUNITY_Community 5|Community 5]]
- [[_COMMUNITY_Community 6|Community 6]]
- [[_COMMUNITY_Community 7|Community 7]]
- [[_COMMUNITY_Community 8|Community 8]]
- [[_COMMUNITY_Community 9|Community 9]]
- [[_COMMUNITY_Community 10|Community 10]]
- [[_COMMUNITY_Community 11|Community 11]]
- [[_COMMUNITY_Community 12|Community 12]]
- [[_COMMUNITY_Community 13|Community 13]]
- [[_COMMUNITY_Community 14|Community 14]]
- [[_COMMUNITY_Community 15|Community 15]]
- [[_COMMUNITY_Community 16|Community 16]]
- [[_COMMUNITY_Community 17|Community 17]]
- [[_COMMUNITY_Community 18|Community 18]]
- [[_COMMUNITY_Community 19|Community 19]]
- [[_COMMUNITY_Community 20|Community 20]]
- [[_COMMUNITY_Community 21|Community 21]]
- [[_COMMUNITY_Community 22|Community 22]]
- [[_COMMUNITY_Community 23|Community 23]]
- [[_COMMUNITY_Community 24|Community 24]]
- [[_COMMUNITY_Community 25|Community 25]]
- [[_COMMUNITY_Community 26|Community 26]]
- [[_COMMUNITY_Community 27|Community 27]]
- [[_COMMUNITY_Community 28|Community 28]]
- [[_COMMUNITY_Community 29|Community 29]]
- [[_COMMUNITY_Community 30|Community 30]]
- [[_COMMUNITY_Community 31|Community 31]]
- [[_COMMUNITY_Community 32|Community 32]]
- [[_COMMUNITY_Community 33|Community 33]]
- [[_COMMUNITY_Community 34|Community 34]]
- [[_COMMUNITY_Community 35|Community 35]]
- [[_COMMUNITY_Community 36|Community 36]]
- [[_COMMUNITY_Community 37|Community 37]]
- [[_COMMUNITY_Community 38|Community 38]]
- [[_COMMUNITY_Community 39|Community 39]]
- [[_COMMUNITY_Community 40|Community 40]]
- [[_COMMUNITY_Community 41|Community 41]]
- [[_COMMUNITY_Community 42|Community 42]]
- [[_COMMUNITY_Community 43|Community 43]]
- [[_COMMUNITY_Community 44|Community 44]]
- [[_COMMUNITY_Community 45|Community 45]]
- [[_COMMUNITY_Community 48|Community 48]]
- [[_COMMUNITY_Community 49|Community 49]]
- [[_COMMUNITY_Community 50|Community 50]]
- [[_COMMUNITY_Community 51|Community 51]]
- [[_COMMUNITY_Community 52|Community 52]]
- [[_COMMUNITY_Community 53|Community 53]]
- [[_COMMUNITY_Community 54|Community 54]]
- [[_COMMUNITY_Community 55|Community 55]]
- [[_COMMUNITY_Community 56|Community 56]]
- [[_COMMUNITY_Community 57|Community 57]]
- [[_COMMUNITY_Community 58|Community 58]]
- [[_COMMUNITY_Community 59|Community 59]]
- [[_COMMUNITY_Community 60|Community 60]]
- [[_COMMUNITY_Community 63|Community 63]]
- [[_COMMUNITY_Community 64|Community 64]]
- [[_COMMUNITY_Community 66|Community 66]]
- [[_COMMUNITY_Community 67|Community 67]]
- [[_COMMUNITY_Community 69|Community 69]]
- [[_COMMUNITY_Community 102|Community 102]]

## God Nodes (most connected - your core abstractions)
1. `main()` - 73 edges
2. `PhysicalLayer()` - 72 edges
3. `begin()` - 71 edges
4. `getActiveModem()` - 55 edges
5. `standby()` - 50 edges
6. `lfs_dir_fetch()` - 47 edges
7. `beginFSK()` - 39 edges
8. `config()` - 35 edges
9. `setOutputPower()` - 34 edges
10. `init()` - 32 edges

## Surprising Connections (you probably didn't know these)
- `app_main()` --calls--> `esp_vfs_littlefs_register()`  [INFERRED]
  main/main.c → managed_components/joltwallet__littlefs/src/esp_littlefs.c
- `app_main()` --calls--> `esp_littlefs_info()`  [INFERRED]
  main/main.c → managed_components/joltwallet__littlefs/src/esp_littlefs.c
- `app_main()` --calls--> `esp_vfs_littlefs_unregister()`  [INFERRED]
  main/main.c → managed_components/joltwallet__littlefs/src/esp_littlefs.c
- `app_main()` --calls--> `led_strip_refresh()`  [INFERRED]
  main/main.c → managed_components/espressif__led_strip/src/led_strip_api.c
- `app_main()` --calls--> `led_strip_clear()`  [INFERRED]
  main/main.c → managed_components/espressif__led_strip/src/led_strip_api.c

## Communities (103 total, 11 thin omitted)

### Community 0 - "Community 0"
Cohesion: 0.06
Nodes (137): lfs1_bd_crc(), lfs1_bd_read(), lfs1_crc(), lfs1_dir_fromle32(), lfs1_dir_next(), lfs1_dir_tole32(), lfs1_entry_fromle32(), lfs1_entry_size() (+129 more)

### Community 1 - "Community 1"
Cohesion: 0.05
Nodes (76): autoSetRxBandwidth(), clearGdo0Action(), clearGdo2Action(), getExpMant(), setBitRateTolerance(), setGdo0Action(), SPIsendCommand(), setPacketAdrs() (+68 more)

### Community 2 - "Community 2"
Cohesion: 0.06
Nodes (78): config_deinit(), init_littlefs(), api_files_info_handler(), api_ota_www_upload_handler(), compute_hash(), esp_littlefs_allocate_fd(), esp_littlefs_by_label(), esp_littlefs_by_partition() (+70 more)

### Community 3 - "Community 3"
Cohesion: 0.06
Nodes (66): lfs_emubd_copy(), lfs_emubd_create(), lfs_emubd_decblock(), lfs_emubd_destroy(), lfs_emubd_erase(), lfs_emubd_incblock(), lfs_emubd_mutblock(), lfs_emubd_powercycles() (+58 more)

### Community 4 - "Community 4"
Cohesion: 0.06
Nodes (58): activateABP(), activateOTAA(), adrBackoff(), beginABP(), beginOTAA(), cadChannelClear(), calculateChannelFlags(), checkBufferCommon() (+50 more)

### Community 5 - "Community 5"
Cohesion: 0.1
Nodes (56): setModulationParamsLoRa(), setRegulatorDCDC(), setRxBoostedGainMode(), SPIcheckStatus(), startCad(), workaroundGFSK(), getStatus(), fixedPacketLengthMode() (+48 more)

### Community 6 - "Community 6"
Cohesion: 0.07
Nodes (47): CC1101(), SPIcommand(), clearIRQ(), disablePipe(), nRF24(), setAddressWidth(), setAutoAck(), setReceivePipe() (+39 more)

### Community 7 - "Community 7"
Cohesion: 0.07
Nodes (40): AFSKClient(), ArduinoHal(), getApbFrequency(), spiFrequencyToClockDiv(), spiTransferByte(), attachInterrupt(), delay(), delayMicroseconds() (+32 more)

### Community 8 - "Community 8"
Cohesion: 0.05
Nodes (17): getGnssAlmanacStatus(), getGnssPosition(), getGnssSatellites(), gnssAbort(), gnssAlmanacReadAddrSize(), gnssAlmanacReadSV(), gnssAlmanacUpdateFromSat(), gnssGetResultSize() (+9 more)

### Community 9 - "Community 9"
Cohesion: 0.12
Nodes (37): beginCommon(), ExternalRadio(), LLCC68(), beginGNSS(), setModulationParamsLrFhss(), getVersionInfo(), roundRampTime(), setRegulatorLDO() (+29 more)

### Community 10 - "Community 10"
Cohesion: 0.04
Nodes (14): clearRxBuffer(), getRandomNumber(), getRxBufferStatus(), lrFhssBuildFrame(), lrFhssSetSyncWord(), setCadParams(), setGfskCrcParams(), setGfskSyncWord() (+6 more)

### Community 11 - "Community 11"
Cohesion: 0.1
Nodes (31): clock_sync_init(), clock_sync_is_synced(), clock_sync_request(), clock_sync_task(), config_get_adr_enabled(), config_get_app_key(), config_get_dev_eui(), config_get_join_eui() (+23 more)

### Community 12 - "Community 12"
Cohesion: 0.19
Nodes (20): int, CodeResult, annotate(), AppendSort, collect(), CovResult, fold(), Frac (+12 more)

### Community 13 - "Community 13"
Cohesion: 0.1
Nodes (23): config_init(), config_load(), config_reset_defaults(), config_save(), _config_save_internal(), config_set_adr_enabled(), config_set_alarm_hum_enabled(), config_set_alarm_hum_high() (+15 more)

### Community 14 - "Community 14"
Cohesion: 0.14
Nodes (11): read(), dropSync(), Bd, Block, hilbert_curve(), lebesgue_curve(), finishRanging(), getRangingResult() (+3 more)

### Community 15 - "Community 15"
Cohesion: 0.11
Nodes (19): config_get_ap_password(), config_get_ap_ssid(), config_get_wifi_ap_mode(), config_get_wifi_password(), config_get_wifi_ssid(), config_set_wifi_ap_mode(), config_set_wifi_ssid(), api_status_handler() (+11 more)

### Community 16 - "Community 16"
Cohesion: 0.16
Nodes (17): AppendSubplot, dat(), dataset(), datasets(), dictify(), escape(), fromargs(), Grid (+9 more)

### Community 17 - "Community 17"
Cohesion: 0.19
Nodes (21): Exception, print(), printFloat(), println(), printNumber(), BenchCase, BenchFailure, BenchSuite (+13 more)

### Community 18 - "Community 18"
Cohesion: 0.13
Nodes (10): api_lorawan_join_handler(), api_lorawan_status_handler(), api_ota_firmware_upload_handler(), api_ota_firmware_url_handler(), api_ota_rollback_handler(), api_ota_status_handler(), get_rollback_possible(), api_sensors_status_handler() (+2 more)

### Community 19 - "Community 19"
Cohesion: 0.15
Nodes (20): alarm_manager_evaluate(), config_get_alarm_hum_enabled(), config_get_alarm_hum_high(), config_get_alarm_hum_low(), config_get_alarm_tc_low(), config_get_alarm_temp_enabled(), config_get_alarm_temp_low(), config_get_hum_correction() (+12 more)

### Community 20 - "Community 20"
Cohesion: 0.14
Nodes (18): alarm_manager_acknowledge(), alarm_manager_get_active_info(), alarm_manager_is_active(), alarm_siren_task(), evaluate_channel(), config_factory_reset(), button_task(), buzzer_beep() (+10 more)

### Community 21 - "Community 21"
Cohesion: 0.16
Nodes (12): BellClient(), transmitDirectAsync(), FSK4Client(), getRawShift(), idle(), setCorrection(), write(), printGlyph() (+4 more)

### Community 22 - "Community 22"
Cohesion: 0.26
Nodes (19): api_files_delete_handler(), api_files_download_handler(), api_files_list_handler(), api_files_mkdir_handler(), api_files_read_handler(), api_files_upload_handler(), api_files_view_handler(), api_files_write_handler() (+11 more)

### Community 23 - "Community 23"
Cohesion: 0.2
Nodes (20): config_get_auto_update_branch(), config_get_auto_update_enabled(), config_get_auto_update_firmware_tag(), config_get_auto_update_www_tag(), api_ota_auto_update_get_handler(), api_ota_auto_update_post_handler(), auto_updater_get_last_check_time(), auto_updater_get_last_result() (+12 more)

### Community 24 - "Community 24"
Cohesion: 0.17
Nodes (16): clearLogs(), closeSidebar(), escapeHtml(), fetchLogs(), formatLogTimestamp(), loadModule(), renderLogs(), startLogPolling() (+8 more)

### Community 26 - "Community 26"
Cohesion: 0.18
Nodes (14): fmCloseEditor(), fmCreateFile(), fmCreateFolder(), fmDelete(), fmEdit(), fmHandleFileSelect(), fmNavigateTo(), fmRefresh() (+6 more)

### Community 27 - "Community 27"
Cohesion: 0.14
Nodes (13): config_get_buzzer_volume(), config_get_web_auth_enabled(), config_get_web_password(), config_get_web_username(), buzzer_init(), configure_led(), app_main(), led_strip_set_pixel() (+5 more)

### Community 28 - "Community 28"
Cohesion: 0.16
Nodes (17): config_get_led_blink_interval_ms(), config_get_led_brightness(), config_get_led_color_error(), config_get_led_color_lorawan(), config_get_led_color_normal(), config_get_led_color_wifi(), apply_brightness(), blink_once() (+9 more)

### Community 29 - "Community 29"
Cohesion: 0.2
Nodes (15): lfs_aligndown(), lfs_alignup(), lfs_crc(), lfs_ctz(), lfs_free(), lfs_frombe32(), lfs_fromle32(), lfs_malloc() (+7 more)

### Community 30 - "Community 30"
Cohesion: 0.22
Nodes (17): addRoundKey(), blockLeftshift(), blockXor(), cipher(), decipher(), decryptECB(), encryptECB(), generateCMAC() (+9 more)

### Community 31 - "Community 31"
Cohesion: 0.24
Nodes (17): connectWiFi(), saveAlarmConfig(), saveBuzzerConfig(), saveSensorConfig(), forceJoin(), auCheckNow(), auSaveConfig(), auSetEnabled() (+9 more)

### Community 32 - "Community 32"
Cohesion: 0.12
Nodes (12): SX1268(), calibrateImage(), calibrateImageRejection(), getDeviceErrors(), fixGFSK(), fixImplicitTimeout(), fixInvertedIQ(), spectralScanAbort() (+4 more)

### Community 33 - "Community 33"
Cohesion: 0.15
Nodes (6): getWifiScanResult(), getWifiScanResultsCount(), startWifiScan(), wifiReadResults(), wifiResetCumulTimings(), wifiScan()

### Community 34 - "Community 34"
Cohesion: 0.14
Nodes (6): APRSClient(), sendFrame(), sendMicE(), sendPosition(), AX25Client(), AX25Frame()

### Community 35 - "Community 35"
Cohesion: 0.17
Nodes (9): api_logs_clear_handler(), api_logs_handler(), add_entry(), log_buffer_clear(), log_buffer_get_since(), log_buffer_get_stats(), log_buffer_init(), log_vprintf_hook() (+1 more)

### Community 36 - "Community 36"
Cohesion: 0.2
Nodes (7): changefile(), changeprefix(), inotifywait(), LinesIO, printProgressBar(), Call in a loop to create terminal progress bar     @params:         iteration, main()

### Community 37 - "Community 37"
Cohesion: 0.14
Nodes (3): id(), type(), infer()

### Community 38 - "Community 38"
Cohesion: 0.16
Nodes (3): cryptoAesDecrypt(), cryptoAesEncrypt(), cryptoCommon()

### Community 39 - "Community 39"
Cohesion: 0.19
Nodes (7): am2315c_init(), am2315c_probe(), detect_sensor(), sht20_init(), sht20_probe(), sht3x_init(), sht3x_probe()

### Community 40 - "Community 40"
Cohesion: 0.46
Nodes (7): mkassert(), p_assert(), p_expr(), p_exprs(), p_stmt(), Parser, write_header()

### Community 41 - "Community 41"
Cohesion: 0.23
Nodes (9): fmUpdateStorageInfo(), initSensors(), refreshSensors(), initSystem(), refreshSystem(), initTasksTab(), refreshTasks(), formatBytes() (+1 more)

### Community 42 - "Community 42"
Cohesion: 0.26
Nodes (9): arrayDump(), debug(), stateDecode(), config(), initConfig(), loadLoRaConfig(), loadSensorConfig(), saveWebAuth() (+1 more)

### Community 43 - "Community 43"
Cohesion: 0.3
Nodes (7): config_get_device_name(), oled_display_init(), oled_display_show_alarm(), oled_display_show_factory_reset(), oled_display_show_system(), oled_display_update(), oled_send_cmd()

### Community 44 - "Community 44"
Cohesion: 0.22
Nodes (4): led_strip_new_rmt_device(), led_strip_rmt_clear(), led_strip_rmt_refresh(), rmt_del_led_strip_encoder()

### Community 45 - "Community 45"
Cohesion: 0.32
Nodes (4): saveLoRaConfig(), initLoRaWAN(), loadLoRaWANConfig(), refreshLoRaWAN()

### Community 48 - "Community 48"
Cohesion: 0.33
Nodes (3): SX1280(), SX1281(), SX1282()

### Community 49 - "Community 49"
Cohesion: 0.33
Nodes (4): SX1272(), SX1273(), SX1277(), SX1278()

### Community 50 - "Community 50"
Cohesion: 0.4
Nodes (3): Si4430(), Si4432(), Si4431()

### Community 51 - "Community 51"
Cohesion: 0.5
Nodes (4): buildLRFHSSPacket(), resetLRFHSS(), setLRFHSSHop(), stepLRFHSS()

### Community 52 - "Community 52"
Cohesion: 0.4
Nodes (5): clearErrors(), getErrors(), setTcxoMode(), setTCXO(), clearDeviceErrors()

### Community 54 - "Community 54"
Cohesion: 0.6
Nodes (3): byteArr(), getBits(), length()

### Community 56 - "Community 56"
Cohesion: 0.4
Nodes (5): Graph Output, Graphify Extraction, Repo Setup, Setup Graphify CLI, Setup Ollama

### Community 63 - "Community 63"
Cohesion: 0.67
Nodes (3): bootWriteFlashEncrypted(), writeCommon(), writeRegMem32()

### Community 64 - "Community 64"
Cohesion: 0.67
Nodes (3): bleBeaconCommon(), bleBeaconSend(), configBleBeacon()

## Knowledge Gaps
- **8 isolated node(s):** `Call in a loop to create terminal progress bar     @params:         iteration`, `EmulatedRadio`, `TestHal`, `Stm32wlxHal`, `Image` (+3 more)
  These have ≤1 connection - possible missing edges or undocumented components.
- **11 thin communities (<3 nodes) omitted from report** — run `graphify query` to explore isolated nodes.

## Suggested Questions
_Questions this graph is uniquely positioned to answer:_

- **Why does `main()` connect `Community 36` to `Community 3`, `Community 37`, `Community 40`, `Community 12`, `Community 14`, `Community 16`, `Community 17`, `Community 21`, `Community 25`, `Community 27`?**
  _High betweenness centrality (0.358) - this node is a cross-community bridge._
- **Why does `app_main()` connect `Community 27` to `Community 2`, `Community 35`, `Community 11`, `Community 13`, `Community 15`, `Community 20`, `Community 23`, `Community 28`?**
  _High betweenness centrality (0.302) - this node is a cross-community bridge._
- **Why does `write()` connect `Community 21` to `Community 36`, `Community 6`, `Community 7`, `Community 14`, `Community 17`?**
  _High betweenness centrality (0.260) - this node is a cross-community bridge._
- **Are the 7 inferred relationships involving `main()` (e.g. with `Tag` and `id()`) actually correct?**
  _`main()` has 7 INFERRED edges - model-reasoned connections that need verification._
- **Are the 3 inferred relationships involving `PhysicalLayer()` (e.g. with `CC1101()` and `ExternalRadio()`) actually correct?**
  _`PhysicalLayer()` has 3 INFERRED edges - model-reasoned connections that need verification._
- **Are the 9 inferred relationships involving `getActiveModem()` (e.g. with `setBandwidth()` and `setSpreadingFactor()`) actually correct?**
  _`getActiveModem()` has 9 INFERRED edges - model-reasoned connections that need verification._
- **What connects `Call in a loop to create terminal progress bar     @params:         iteration`, `EmulatedRadio`, `TestHal` to the rest of the system?**
  _8 weakly-connected nodes found - possible documentation gaps or missing edges._