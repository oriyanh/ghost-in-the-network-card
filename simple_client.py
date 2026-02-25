import requests
import pprint
import time
import secrets

def get_request(url, cookies=None):
    if cookies is not None:
        resp = requests.get(url, cookies=cookies)
    else:
        resp = requests.get(url)
    pprint.pprint(f"Response headers {url}:")
    print(f"{resp.headers}\n")
    pprint.pprint(f"Requst headers url {url}:")
    print(f"{resp.request.headers}\n")
    pprint.pprint(f"Response cookies url {url}:")
    print(f"{resp.cookies}\n")
    if resp.cookies:
        return resp.cookies
    return None

if __name__ == "__main__":
    cookies = None
    while 1:
        try:
            cookies = get_request("http://10.10.1.1:80/cookies", cookies)
            session_id = secrets.token_urlsafe(6)  # Generates 8 random bytes
            print(f"rx_sessionId={cookies.get('sessionId')}, tx_sessionId={session_id}\n\n")
            cookies['sessionId'] = session_id
        except:
            print("Connection failure, trying again")
        time.sleep(5)