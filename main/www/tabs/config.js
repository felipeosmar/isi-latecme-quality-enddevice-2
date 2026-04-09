// Config Module

// ============================================================================
// WiFi Configuration
// ============================================================================

async function scanWiFi() {
    try {
        const d = await api('wifi/scan');
        const sel = document.getElementById('wifi-select');
        sel.innerHTML = '<option value="">Select network...</option>';
        // Backend returns an array directly, not {networks: [...]}
        const networks = Array.isArray(d) ? d : (d.networks || []);
        networks.forEach(n => {
            const opt = document.createElement('option');
            opt.value = n.ssid;
            opt.textContent = `${n.ssid} (${n.rssi}dBm)`;
            sel.appendChild(opt);
        });
        toast(`Scan complete: ${networks.length} network(s) found`, 'success');
    } catch (e) {
        toast('WiFi scan failed', 'error');
    }
}

async function connectWiFi() {
    const ssid = document.getElementById('wifi-select').value;
    const pass = document.getElementById('wifi-pass').value;
    if (!ssid) { toast('Select a network', 'error'); return; }

    try {
        const r = await api('wifi/connect', 'POST', { ssid, password: pass });
        if (r.success) {
            toast('Connecting... Device will restart.', 'success');
            setTimeout(() => location.reload(), 5000);
        } else {
            toast(r.message || 'Connection failed', 'error');
        }
    } catch (e) {
        toast('Failed to connect', 'error');
    }
}

// ============================================================================
// Sensor Configuration
// ============================================================================

async function loadSensorConfig() {
    try {
        const d = await api('sensors/config');
        document.getElementById('cfg-sensor-interval').value = d.interval || 30;
        document.getElementById('cfg-temp-corr').value = d.temp_correction || 0;
        document.getElementById('cfg-hum-corr').value = d.hum_correction || 0;
        document.getElementById('cfg-device-name').value = d.device_name || 'sensor-01';
        document.getElementById('cfg-thermocouple').checked = d.thermocouple_enabled !== false;
        document.getElementById('cfg-tc-max-temp').value = d.thermocouple_max_temp || 200;
        document.getElementById('cfg-tc-min-temp').value = d.thermocouple_min_temp || 0;
        document.getElementById('cfg-tc-correction').value = d.thermocouple_correction || 0;
        const vol = d.buzzer_volume !== undefined ? d.buzzer_volume : 80;
        document.getElementById('cfg-buzzer-vol').value = vol;
        document.getElementById('cfg-buzzer-vol-val').textContent = vol;
        document.getElementById('cfg-alarm-temp-en').checked    = d.alarm_temp_enabled || false;
        document.getElementById('cfg-alarm-temp-low').value     = d.alarm_temp_low  !== undefined ? d.alarm_temp_low  : 0;
        document.getElementById('cfg-alarm-temp-high').value    = d.alarm_temp_high !== undefined ? d.alarm_temp_high : 0;
        document.getElementById('cfg-alarm-hum-en').checked     = d.alarm_hum_enabled || false;
        document.getElementById('cfg-alarm-hum-low').value      = d.alarm_hum_low   !== undefined ? d.alarm_hum_low   : 0;
        document.getElementById('cfg-alarm-hum-high').value     = d.alarm_hum_high  !== undefined ? d.alarm_hum_high  : 0;
        document.getElementById('cfg-alarm-tc-en').checked      = d.alarm_tc_enabled || false;
        document.getElementById('cfg-alarm-tc-low').value       = d.alarm_tc_low    !== undefined ? d.alarm_tc_low    : 0;
        document.getElementById('cfg-alarm-tc-high').value      = d.alarm_tc_high   !== undefined ? d.alarm_tc_high   : 0;
        const tcHw = d.thermocouple_hw_enabled === true;
        const tcCfg = document.getElementById('tc-config-section');
        const tcAlarm = document.getElementById('tc-alarm-section');
        if (tcCfg) tcCfg.classList.toggle('hidden', !tcHw);
        if (tcAlarm) tcAlarm.classList.toggle('hidden', !tcHw);
    } catch (e) {
        console.error('Failed to load sensor config:', e);
    }
}

async function saveSensorConfig() {
    const config = {
        interval: parseInt(document.getElementById('cfg-sensor-interval').value),
        temp_correction: parseFloat(document.getElementById('cfg-temp-corr').value),
        hum_correction: parseFloat(document.getElementById('cfg-hum-corr').value),
        device_name: document.getElementById('cfg-device-name').value.trim(),
        thermocouple_enabled: document.getElementById('cfg-thermocouple').checked,
        thermocouple_max_temp: parseFloat(document.getElementById('cfg-tc-max-temp').value),
        thermocouple_min_temp: parseFloat(document.getElementById('cfg-tc-min-temp').value),
        thermocouple_correction: parseFloat(document.getElementById('cfg-tc-correction').value)
    };

    try {
        const r = await api('sensors/config', 'POST', config);
        if (r.success) {
            toast('Sensor config saved', 'success');
        } else {
            toast(r.message || 'Failed to save', 'error');
        }
    } catch (e) {
        toast('Failed to save sensor config', 'error');
    }
}

// ============================================================================
// Buzzer Configuration
// ============================================================================

async function saveBuzzerConfig() {
    const config = {
        buzzer_volume: parseInt(document.getElementById('cfg-buzzer-vol').value)
    };

    try {
        const r = await api('sensors/config', 'POST', config);
        if (r.success) {
            toast('Buzzer config saved', 'success');
        } else {
            toast(r.message || 'Failed to save', 'error');
        }
    } catch (e) {
        toast('Failed to save buzzer config', 'error');
    }
}

// ============================================================================
// Alarm Configuration
// ============================================================================

async function saveAlarmConfig() {
    const config = {
        alarm_temp_enabled: document.getElementById('cfg-alarm-temp-en').checked,
        alarm_temp_low:     parseFloat(document.getElementById('cfg-alarm-temp-low').value),
        alarm_temp_high:    parseFloat(document.getElementById('cfg-alarm-temp-high').value),
        alarm_hum_enabled:  document.getElementById('cfg-alarm-hum-en').checked,
        alarm_hum_low:      parseFloat(document.getElementById('cfg-alarm-hum-low').value),
        alarm_hum_high:     parseFloat(document.getElementById('cfg-alarm-hum-high').value),
        alarm_tc_enabled:   document.getElementById('cfg-alarm-tc-en').checked,
        alarm_tc_low:       parseFloat(document.getElementById('cfg-alarm-tc-low').value),
        alarm_tc_high:      parseFloat(document.getElementById('cfg-alarm-tc-high').value),
    };

    try {
        const r = await api('sensors/config', 'POST', config);
        if (r.success) {
            toast('Configuração de alarmes salva', 'success');
        } else {
            toast(r.message || 'Falha ao salvar', 'error');
        }
    } catch (e) {
        toast('Falha ao salvar alarmes', 'error');
    }
}

// ============================================================================
// LoRaWAN Configuration
// ============================================================================

async function loadLoRaConfig() {
    try {
        const d = await api('lorawan/config');
        document.getElementById('cfg-dev-eui').value = d.dev_eui || '';
        document.getElementById('cfg-join-eui').value = d.join_eui || '';
        document.getElementById('cfg-app-key').value = d.app_key || '';
        document.getElementById('cfg-lora-port').value = d.port || 1;
        document.getElementById('cfg-uplink-interval').value = d.uplink_interval || 60;
        document.getElementById('cfg-sub-band').value = d.sub_band || 2;
        document.getElementById('cfg-adr').checked = d.adr_enabled !== false;
    } catch (e) {
        console.error('Failed to load LoRaWAN config:', e);
    }
}

async function saveLoRaConfig() {
    const config = {
        dev_eui: document.getElementById('cfg-dev-eui').value.trim(),
        join_eui: document.getElementById('cfg-join-eui').value.trim(),
        app_key: document.getElementById('cfg-app-key').value.trim(),
        port: parseInt(document.getElementById('cfg-lora-port').value),
        uplink_interval: parseInt(document.getElementById('cfg-uplink-interval').value),
        sub_band: parseInt(document.getElementById('cfg-sub-band').value),
        adr_enabled: document.getElementById('cfg-adr').checked
    };

    try {
        const r = await api('lorawan/config', 'POST', config);
        if (r.success) {
            toast('LoRaWAN config saved. Restarting...', 'success');
            setTimeout(() => location.reload(), 3000);
        } else {
            toast(r.message || 'Failed to save', 'error');
        }
    } catch (e) {
        toast('Failed to save LoRaWAN config', 'error');
    }
}

// ============================================================================
// Web Auth Configuration
// ============================================================================

async function loadWebAuth() {
    // Web auth is read from the status endpoint or a dedicated endpoint
    // For now, just load defaults from the form
}

async function saveWebAuth() {
    toast('Web auth config will be saved on next restart', 'info');
}

// ============================================================================
// Init
// ============================================================================

function initConfig() {
    scanWiFi();
    loadSensorConfig();
    loadLoRaConfig();

    // Buzzer volume slider live update
    const volSlider = document.getElementById('cfg-buzzer-vol');
    if (volSlider) {
        volSlider.addEventListener('input', function() {
            document.getElementById('cfg-buzzer-vol-val').textContent = this.value;
        });
    }
}

registerModule('config', initConfig);
