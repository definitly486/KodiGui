(function() {
    const originalGenerateRequest = MediaKeySession.prototype.generateRequest;
    MediaKeySession.prototype.generateRequest = function(initDataType, initData) {
        if (initDataType === 'cenc' || initDataType === 'webm') {
            const uint8array = new Uint8Array(initData);
            let binary = '';
            for (let i = 0; i < uint8array.byteLength; i++) {
                binary += String.fromCharCode(uint8array[i]);
            }
            const psshBase64 = btoa(binary);
            localStorage.setItem('wv_last_pssh', psshBase64);
            console.log('WIDEVINE_CAPTURE: PSSH saved');
        }
        return originalGenerateRequest.call(this, initDataType, initData);
    };
})();