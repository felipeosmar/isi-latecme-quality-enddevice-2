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
        document.getElementById('cfg-tc-sck').value = d.thermocouple_sck_pin || 32;
        document.getElementById('cfg-tc-so').value = d.thermocouple_so_pin || 35;
        document.getElementById('cfg-tc-cs').value = d.thermocouple_cs_pin || 33;
        const vol = d.buzzer_volume !== undefined ? d.buzzer_volume : 80;
        document.getElementById('cfg-buzzer-vol').value = vol;
        document.getElementById('cfg-buzzer-vol-val').textContent = vol;
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
        thermocouple_sck_pin: parseInt(document.getElementById('cfg-tc-sck').value),
        thermocouple_so_pin: parseInt(document.getElementById('cfg-tc-so').value),
        thermocouple_cs_pin: parseInt(document.getElementById('cfg-tc-cs').value),
        buzzer_volume: parseInt(document.getElementById('cfg-buzzer-vol').value)
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
