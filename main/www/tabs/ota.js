// OTA Tab Module
let otaStatusTimer = null;
let otaWasInProgress = false;
let otaUnreachableCount = 0;

async function refreshOtaStatus() {
    try {
        const d = await api('ota/status');
        otaUnreachableCount = 0;
        document.getElementById('ota-partition').textContent = d.current_partition || '-';
        document.getElementById('ota-app-version').textContent = d.app_version || '-';
        document.getElementById('ota-idf-version').textContent = d.idf_version || '-';

        const stateMap = { idle: 'Idle', in_progress: 'In Progress', rebooting: 'Rebooting', failed: 'Failed' };
        document.getElementById('ota-state').textContent = stateMap[d.state] || d.state || '-';

        const rollbackSection = document.getElementById('ota-rollback-section');
        if (d.rollback_possible) {
            rollbackSection.classList.remove('hidden');
        } else {
            rollbackSection.classList.add('hidden');
        }

        if (d.state === 'in_progress' && d.total_bytes > 0) {
            otaWasInProgress = true;
            const pct = Math.round((d.bytes_written / d.total_bytes) * 100);
            document.getElementById('ota-firmware-bar').style.width = pct + '%';
            document.getElementById('ota-firmware-status-msg').textContent =
                `Downloading... ${pct}% (${d.bytes_written} / ${d.total_bytes} bytes)`;
            document.getElementById('ota-firmware-progress').classList.remove('hidden');
        } else {
            otaWasInProgress = false;
        }
        if (d.state === 'rebooting') {
            document.getElementById('ota-firmware-status-msg').textContent = 'Update complete! Device rebooting...';
            document.getElementById('ota-firmware-progress').classList.remove('hidden');
        } else if (d.state === 'failed') {
            document.getElementById('ota-firmware-status-msg').textContent = 'Error: ' + (d.error || 'Unknown');
            document.getElementById('ota-firmware-progress').classList.remove('hidden');
        }
    } catch (e) {
        if (otaWasInProgress) {
            otaUnreachableCount++;
            if (otaUnreachableCount >= 2) {
                document.getElementById('ota-firmware-bar').style.width = '100%';
                document.getElementById('ota-firmware-status-msg').textContent = 'Update complete! Device rebooting...';
                document.getElementById('ota-firmware-progress').classList.remove('hidden');
                otaWasInProgress = false;
            }
        }
    }

    try {
        const au = await api('ota/auto-update');

        // Release field in Current Firmware card
        const releaseEl = document.getElementById('ota-release');
        if (releaseEl) releaseEl.textContent = au.firmware_tag || '-';

        // Auto Update card
        const btnOn = document.getElementById('au-btn-on');
        const btnOff = document.getElementById('au-btn-off');
        if (btnOn && btnOff) {
            btnOn.classList.toggle('active', au.enabled === true);
            btnOff.classList.toggle('active', au.enabled === false);
        }

        const branchEl = document.getElementById('au-branch');
        if (branchEl && document.activeElement !== branchEl) {
            branchEl.value = au.branch || '';
        }

        document.getElementById('au-firmware-tag').textContent = au.firmware_tag || '-';
        document.getElementById('au-www-tag').textContent = au.www_tag || '-';

        const lastCheckEl = document.getElementById('au-last-check');
        if (au.last_check_time && au.last_check_time > 0) {
            lastCheckEl.textContent = new Date(au.last_check_time * 1000).toLocaleString();
        } else {
            lastCheckEl.textContent = 'Never';
        }

        const resultMap = {
            never: 'Never checked',
            up_to_date: 'Up to date',
            updated: 'Updated',
            error: 'Error'
        };
        document.getElementById('au-last-result').textContent = resultMap[au.last_check_result] || au.last_check_result || '-';
    } catch (e) {
        // Auto-update API may not be available
    }
}

function initOta() {
    refreshOtaStatus();
    if (otaStatusTimer) clearInterval(otaStatusTimer);
    otaStatusTimer = setInterval(refreshOtaStatus, 3000);
}

async function auSetEnabled(enabled) {
    try {
        await api('ota/auto-update', 'POST', { enabled });
        document.getElementById('au-btn-on').classList.toggle('active', enabled === true);
        document.getElementById('au-btn-off').classList.toggle('active', enabled === false);
        toast(enabled ? 'Auto update enabled' : 'Auto update disabled', 'success');
    } catch (e) {
        toast('Failed to update setting', 'error');
    }
}

async function auSaveConfig() {
    const branch = document.getElementById('au-branch').value.trim();
    if (!branch) {
        toast('Branch cannot be empty', 'error');
        return;
    }
    try {
        await api('ota/auto-update', 'POST', { branch });
        toast('Config saved', 'success');
    } catch (e) {
        toast('Failed to save config', 'error');
    }
}

async function auCheckNow() {
    const btn = document.getElementById('au-check-btn');
    if (btn) btn.disabled = true;
    try {
        await api('ota/auto-update', 'POST', { trigger_now: true });
        toast('Check triggered', 'success');
    } catch (e) {
        toast('Failed to trigger check', 'error');
    } finally {
        if (btn) setTimeout(() => { btn.disabled = false; }, 3000);
    }
}

async function otaUploadFirmware() {
    const fileInput = document.getElementById('ota-firmware-file');
    if (!fileInput.files.length) {
        toast('Select a .bin file first', 'error');
        return;
    }
    const file = fileInput.files[0];
    const progressDiv = document.getElementById('ota-firmware-progress');
    const statusMsg = document.getElementById('ota-firmware-status-msg');
    const bar = document.getElementById('ota-firmware-bar');

    progressDiv.classList.remove('hidden');
    statusMsg.textContent = 'Uploading...';
    bar.style.width = '0%';

    const xhr = new XMLHttpRequest();
    xhr.open('POST', '/api/ota/firmware/upload');

    xhr.upload.onprogress = function(e) {
        if (e.lengthComputable) {
            const pct = Math.round((e.loaded / e.total) * 100);
            bar.style.width = pct + '%';
            statusMsg.textContent = `Uploading... ${pct}%`;
        }
    };

    xhr.onload = function() {
        if (xhr.status === 200) {
            bar.style.width = '100%';
            statusMsg.textContent = 'Upload complete! Device rebooting...';
            toast('Firmware update started', 'success');
        } else {
            statusMsg.textContent = 'Error: ' + (xhr.responseText || xhr.status);
            toast('OTA failed', 'error');
        }
    };

    xhr.onerror = function() {
        statusMsg.textContent = 'Connection lost (device may be rebooting)';
    };

    xhr.send(file);
}

async function otaFirmwareUrl() {
    const url = document.getElementById('ota-firmware-url').value.trim();
    if (!url) {
        toast('Enter a URL', 'error');
        return;
    }
    otaWasInProgress = false;
    otaUnreachableCount = 0;
    const statusMsg = document.getElementById('ota-firmware-status-msg');
    const progressDiv = document.getElementById('ota-firmware-progress');
    progressDiv.classList.remove('hidden');
    statusMsg.textContent = 'Starting OTA from URL...';

    try {
        const d = await api('ota/firmware/url', 'POST', { url });
        if (d.success) {
            statusMsg.textContent = 'OTA in progress, downloading...';
            toast('OTA started', 'success');
        } else {
            statusMsg.textContent = 'Failed to start OTA';
            toast('OTA failed', 'error');
        }
    } catch (e) {
        statusMsg.textContent = 'Error: ' + e.message;
        toast('OTA failed', 'error');
    }
}

async function otaUploadWww() {
    const fileInput = document.getElementById('ota-www-file');
    if (!fileInput.files.length) {
        toast('Select a www.bin file first', 'error');
        return;
    }
    const file = fileInput.files[0];
    const statusMsg = document.getElementById('ota-www-status-msg');
    const progressDiv = document.getElementById('ota-www-progress');
    progressDiv.classList.remove('hidden');
    statusMsg.textContent = 'Uploading www partition...';

    try {
        const resp = await fetch('/api/ota/www/upload', {
            method: 'POST',
            body: file
        });
        const d = await resp.json();
        if (d.success) {
            statusMsg.textContent = 'Done! Device rebooting with new web UI...';
            toast('Web UI update complete', 'success');
        } else {
            statusMsg.textContent = 'Failed';
            toast('www update failed', 'error');
        }
    } catch (e) {
        statusMsg.textContent = 'Connection lost (device may be rebooting)';
    }
}

async function otaRollback() {
    if (!confirm('Roll back to the previous firmware version?')) return;
    try {
        const d = await api('ota/rollback', 'POST');
        if (d.success) {
            toast('Rolling back...', 'success');
        } else {
            toast(d.message || 'Rollback not available', 'error');
        }
    } catch (e) {
        toast('Device rebooting', 'success');
    }
}

registerModule('ota', initOta);
