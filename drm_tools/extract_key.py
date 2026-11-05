import sys
import os
import json

sys.path.insert(0, os.path.join(os.path.dirname(__file__), 'libs', 'wheels'))

try:
    from pywidevine.device import Device
    from pywidevine.pssh import PSSH
    from pywidevine.decrypt.wvdecryptcustom import WvDecrypt
    import requests
except ImportError as e:
    print(f"ERROR_IMPORT: {e}")
    sys.exit(1)

def main():
    if len(sys.argv) < 5:
        print("USAGE: python3 extract_key.py <pssh_b64> <lic_url> <lic_headers_json> <device_wvd_path>")
        sys.exit(1)

    pssh_b64 = sys.argv[1]
    lic_url = sys.argv[2]
    lic_headers_json = sys.argv[3]
    device_path = sys.argv[4]

    try:
        device = Device.load(device_path)
        pssh_obj = PSSH(pssh_b64)
        wvdecrypt = WvDecrypt(init_data_b64=pssh_b64, cert_data_b64=None, device=device)
        challenge = wvdecrypt.get_challenge()
        
        headers = json.loads(lic_headers_json) if lic_headers_json else {"Content-Type": "application/octet-stream"}
        response = requests.post(lic_url, data=challenge, headers=headers, timeout=15)
        
        if response.status_code == 200:
            wvdecrypt.update_license(response.content)
            keys = wvdecrypt.start_process()
            if keys:
                for key in keys:
                    print(f"KEY_FOUND:{key.kid.hex()}:{key.key.hex()}")
            else:
                print("ERROR: Ключи не найдены")
        else:
            print(f"ERROR_LICENSE: HTTP {response.status_code}")
            
    except Exception as e:
        print(f"ERROR_EXCEPTION: {str(e)}")

if __name__ == "__main__":
    main()