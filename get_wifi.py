with open('.wifi_credentials', 'r') as f:
    ssid, pw = f.readline().split(',')
    print(" -DWIFI_SSID=\\\"%s\\\" -DWIFI_PASSWORD=\\\"%s\\\"\n" % (ssid, pw))