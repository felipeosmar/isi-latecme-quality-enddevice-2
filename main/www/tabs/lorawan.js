// LoRaWAN Tab Module
let lorawanPollTimer = null;

async function refreshLoRaWAN() {
    try {
        const d = await api('lorawan/status');
        document.getElementById('lora-join-status').textContent = d.joined ? 'Joined' : 'Not Joined';
        document.getElementById('lora-join-status').className = d.joined ? 'text-success' : 'text-warning';
        document.getElementById('lora-devaddr').textContent = d.dev_addr || '--';
        document.getElementById('lora-uplink-count').textContent = d.uplink_count || 0;
        document.getElementById('lora-last-uplink').textContent = d.last_uplink_time ? formatUptime(d.last_uplink_time) : '--';
        document.getElementById('lora-rssi').textContent = d.rssi ? d.rssi + ' dBm' : '-- dBm';
        document.getElementById('lora-snr').textContent = d.snr ? d.snr.toFixed(1) + ' dB' : '-- dB';
        document.getElementById('lora-data-rate').textContent = d.data_rate !== undefined ? 'DR' + d.data_rate : '--';
    } catch (e) {
        console.error('Failed to refresh LoRaWAN status:', e);
    }
}

async function loadLoRaWANConfig() {
    try {
        const d = await api('lorawan/config');
        document.getElementById('lora-dev-eui').value = d.dev_eui || '';
        document.getElementById('lora-join-eui').value = d.join_eui || '';
        document.getElementById('lora-app-key').value = d.app_key || '';
        document.getElementById('lora-port').value = d.port || 1;
        document.getElementById('lora-interval').value = d.uplink_interval || 60;
        document.getElementById('lora-sub-band').value = d.sub_band || 2;
        document.getElementById('lora-adr').checked = d.adr_enabled !== false;
    } catch (e) {
        console.error('Failed to load LoRaWAN config:', e);
    }
}

async function saveLoRaWANConfig() {
    const config = {
        dev_eui: document.getElementById('lora-dev-eui').value.trim(),
        join_eui: document.getElementById('lora-join-eui').value.trim(),
        app_key: document.getElementById('lora-app-key').value.trim(),
        port: parseInt(document.getElementById('lora-port').value),
        uplink_interval: parseInt(document.getElementById('lora-interval').value),
        sub_band: parseInt(document.getElementById('lora-sub-band').value),
        adr_enabled: document.getElementById('lora-adr').checked
    };

    try {
        const r = await api('lorawan/config', 'POST', config);
        if (r.success) {
            toast('LoRaWAN config saved. Restarting...', 'success');
            setTimeout(() => location.reload(), 3000);
        } else {
            toast(r.message || 'Failed to save config', 'error');
        }
    } catch (e) {
        toast('Failed to save LoRaWAN config', 'error');
    }
}

async function forceJoin() {
    try {
        const r = await api('lorawan/join', 'POST');
        toast(r.success ? 'Join request sent' : (r.message || 'Failed'), r.success ? 'success' : 'error');
    } catch (e) {
        toast('Failed to send join request', 'error');
    }
}

function initLoRaWAN() {
    refreshLoRaWAN();
    loadLoRaWANConfig();
    if (lorawanPollTimer) clearInterval(lorawanPollTimer);
    lorawanPollTimer = setInterval(refreshLoRaWAN, 5000);
}

registerModule('lorawan', initLoRaWAN);
