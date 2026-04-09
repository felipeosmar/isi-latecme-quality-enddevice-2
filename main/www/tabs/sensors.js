// Sensors Tab Module

async function refreshSensors() {
    try {
        const d = await api('sensors/status');
        document.getElementById('sensor-name').textContent = d.sensor_name || 'Not detected';
        document.getElementById('sensor-temp').textContent = d.temp_hum_valid ? d.temperature.toFixed(1) + ' \u00b0C' : 'N/A';
        document.getElementById('sensor-hum').textContent = d.temp_hum_valid ? d.humidity.toFixed(1) + ' %' : 'N/A';
        document.getElementById('sensor-last-read').textContent = d.timestamp_ms ? formatUptime(d.timestamp_ms) : '--';

        const tcCard = document.getElementById('thermocouple-card');
        if (tcCard) tcCard.classList.toggle('hidden', !d.thermocouple_hw_enabled);
        if (d.thermocouple_hw_enabled) {
            document.getElementById('tc-temp').textContent = d.thermocouple_valid ? d.thermocouple_temp.toFixed(1) + ' \u00b0C' : 'N/A';
            document.getElementById('tc-status').textContent = d.thermocouple_valid ? 'Connected' : 'Not connected';
        }
    } catch (e) {
        console.error('Failed to refresh sensors:', e);
    }

    try {
        const c = await api('sensors/config');
        document.getElementById('sensor-interval').textContent = c.interval + ' s';
        document.getElementById('sensor-temp-corr').textContent = c.temp_correction.toFixed(1) + ' \u00b0C';
        document.getElementById('sensor-hum-corr').textContent = c.hum_correction.toFixed(1) + ' %';
    } catch (e) {}
}

function initSensors() {
    refreshSensors();
}

registerModule('sensors', initSensors, { pollFn: refreshSensors, pollInterval: 5000 });
