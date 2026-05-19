#!/usr/bin/env python3
"""刷新阿里云 NLS Token 并更新 ~/.profile

用法:
  source ~/.bashrc  # 确保 ALIYUN_ACCESS_KEY/SECRET 可用
  python3 scripts/refresh_nls_token.py
"""

import os
import sys
import json
import hmac
import hashlib
import base64
import uuid
import urllib.parse
import requests
from datetime import datetime, timezone

# 阿里云 NLS Token API 端点 (注意: 不是 nls-cloud-meta)
NLS_META_ENDPOINT = "nls-meta.cn-shanghai.aliyuncs.com"


def percent_encode(s):
    return urllib.parse.quote(s, safe="~")


def compute_signature(method, params, access_key_secret):
    sorted_params = sorted(params.items())
    query_string = "&".join(
        f"{percent_encode(k)}={percent_encode(v)}" for k, v in sorted_params
    )
    string_to_sign = f"{method}&{percent_encode('/')}&{percent_encode(query_string)}"
    key = (access_key_secret + "&").encode("utf-8")
    signature = hmac.new(key, string_to_sign.encode("utf-8"), hashlib.sha1).digest()
    return base64.b64encode(signature).decode("utf-8")


def create_token(access_key_id, access_key_secret, proxies=None):
    params = {
        "Action": "CreateToken",
        "Format": "JSON",
        "Version": "2019-02-28",
        "AccessKeyId": access_key_id,
        "SignatureMethod": "HMAC-SHA1",
        "SignatureVersion": "1.0",
        "SignatureNonce": str(uuid.uuid4()),
        "Timestamp": datetime.now(timezone.utc).strftime("%Y-%m-%dT%H:%M:%SZ"),
    }
    params["Signature"] = compute_signature("GET", params, access_key_secret)

    session = requests.Session()
    if proxies:
        session.proxies = proxies

    resp = session.get(
        f"https://{NLS_META_ENDPOINT}/",
        params=params,
        timeout=10,
    )
    resp.raise_for_status()
    return resp.json()


def update_profile(token):
    profile_path = os.path.expanduser("~/.profile")
    with open(profile_path, "r") as f:
        lines = f.readlines()

    found = False
    for i, line in enumerate(lines):
        if line.strip().startswith("export ALIBABA_NLS_TOKEN="):
            lines[i] = f'export ALIBABA_NLS_TOKEN="{token}"\n'
            found = True
            break

    if not found:
        lines.append(f'export ALIBABA_NLS_TOKEN="{token}"\n')

    with open(profile_path, "w") as f:
        f.writelines(lines)


def main():
    ak = os.environ.get("ALIYUN_ACCESS_KEY")
    secret = os.environ.get("ALIYUN_ACCESS_SECRET")

    if not ak or not secret:
        print("错误: 请设置 ALIYUN_ACCESS_KEY 和 ALIYUN_ACCESS_SECRET 环境变量")
        sys.exit(1)

    # 检测是否需要代理 (有 http_proxy 环境变量时使用)
    http_proxy = os.environ.get("http_proxy") or os.environ.get("HTTP_PROXY")
    proxies = None
    if http_proxy:
        proxies = {"http": http_proxy, "https": http_proxy}
        print(f"使用代理: {http_proxy}")

    print("正在生成 NLS Token...")
    try:
        result = create_token(ak, secret, proxies)
    except Exception as e:
        print(f"生成 Token 失败: {e}")
        sys.exit(1)

    token_data = result.get("Token", {})
    token = token_data.get("Id")
    expire_time = token_data.get("ExpireTime")

    if not token:
        errmsg = result.get("ErrMsg", "")
        print(f"未能获取 Token: {errmsg}")
        print(f"响应: {json.dumps(result, ensure_ascii=False, indent=2)}")
        sys.exit(1)

    expire_str = (
        datetime.fromtimestamp(expire_time).strftime("%Y-%m-%d %H:%M:%S")
        if expire_time
        else "未知"
    )

    print(f"新 Token: {token}")
    print(f"过期时间: {expire_str}")

    update_profile(token)
    print("已更新 ~/.profile")

    os.environ["ALIBABA_NLS_TOKEN"] = token
    print("已更新当前环境变量")

    print("\n请执行以下命令使新 Token 生效:")
    print("  source ~/.profile")
    print("  然后重启 agent 程序")


if __name__ == "__main__":
    main()
