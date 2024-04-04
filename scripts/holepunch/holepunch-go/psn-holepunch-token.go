package main

import (
	"github.com/pkg/browser"
	"encoding/json"
	"encoding/hex"
	"crypto/rand"
	"net/http"
	"net/url"
	"strings"
	"strconv"
	"bufio"
	"flag"
	"fmt"
	"log"
	"os"
)

func generateDuid()(string) {
	prefix := "0000000700410080"
	bytes := make([]byte, 16)
	if _, err := rand.Read(bytes); err != nil {
		log.Fatal(err)
	}
		return (prefix + hex.EncodeToString(bytes))
}

func main(){
	headless := flag.Bool("headless", false, "Operates in Headless mode")
	flag.Parse()
	fmt.Println(
`== PSN ID Scraper for Remote Play ==
In order to get your Account code for Remote Play, You'll need to Login via a special Remote Play login webpage.
After logging in, you will see a webpage that displays "redirect" in the top-left.
When you see this page, Copy the entire URL from your browser, paste it below and then press *Enter*
`)
    duid := generateDuid()
	fmt.Printf("Duid: %s with length: %d", duid, len(duid))
	reader := bufio.NewReader(os.Stdin)
	if *headless {
		fmt.Printf("[Headless] You'll need to open this page in a web browser that supports Javascript/ReCaptcha\n[Headless] https://auth.api.sonyentertainmentnetwork.com/2.0/oauth/authorize?service_entity=urn:service-entity:psn&response_type=code&client_id=ba495a24-818c-472b-b12d-ff231c1b5745&redirect_uri=https://remoteplay.dl.playstation.net/remoteplay/redirect&scope=psn:clientapp referenceDataService:countryConfig.read pushNotification:webSocket.desktop.connect sessionManager:remotePlaySession.system.update&duid=%s&request_locale=en_US&ui=pr&service_logo=ps&layout_type=popup&smcid=remoteplay&prompt=always&PlatformPrivacyWs1=minimal&", duid)
	} else {
		fmt.Printf("Press Enter to open the PSN Remote Play login webpage in your browser")
		reader.ReadString('\n')
		browser.OpenURL("https://auth.api.sonyentertainmentnetwork.com/2.0/oauth/authorize?service_entity=urn:service-entity:psn&response_type=code&client_id=ba495a24-818c-472b-b12d-ff231c1b5745&redirect_uri=https://remoteplay.dl.playstation.net/remoteplay/redirect&scope=psn:clientapp referenceDataService:countryConfig.read pushNotification:webSocket.desktop.connect sessionManager:remotePlaySession.system.update&duid=" + duid + "&request_locale=en_US&ui=pr&service_logo=ps&layout_type=popup&smcid=remoteplay&prompt=always&PlatformPrivacyWs1=minimal&")
	}

	fmt.Print("Awaiting Input >")
	response, _ := reader.ReadString('\n')
	response = strings.TrimSpace(response)
	URL, err := url.Parse(response)
	if err != nil {
		log.Fatal(err)
	}

	query := URL.Query()
	if query.Get("code") == ""{
		log.Fatal("Invalid URL has been submitted")
	}

	client := &http.Client{}
	data :=url.Values{}
	data.Set("grant_type","authorization_code")
	data.Set("code", query.Get("code"))
	data.Set("redirect_uri","https://remoteplay.dl.playstation.net/remoteplay/redirect")
	req, err := http.NewRequest("POST", "https://auth.api.sonyentertainmentnetwork.com/2.0/oauth/token", strings.NewReader(data.Encode()))
	req.SetBasicAuth("ba495a24-818c-472b-b12d-ff231c1b5745", "mvaiZkRsAsI1IBkY")
	req.Header.Add("Content-Type", "application/x-www-form-urlencoded")
	req.Header.Add("Content-Length", strconv.Itoa(len(data.Encode())))
	res, err := client.Do(req)

	defer res.Body.Close()
	access := AccessKeys{}
	err = json.NewDecoder(res.Body).Decode(&access)
	if err != nil {
		log.Fatal(err)
	}
    file, err := os.OpenFile("/home/streetpea/Documents/psn-chiaki/token.txt", os.O_WRONLY|os.O_CREATE, 0666)
    if err != nil {
        fmt.Println("File does not exists or cannot be created")
        os.Exit(1)
    }
    defer file.Close()

    w := bufio.NewWriter(file)
    fmt.Fprintf(w, "Access Token: %s\n", access.AccessToken)
    w.Flush()

	fmt.Fprintf(w, "Refresh Token: %v\n", access.RefreshToken)
	w.Flush()

	fmt.Fprintf(w, "Expiry Date: %v\n", access.ExpiryDate)
	w.Flush()

	fmt.Println("Your credentials are saved to: /home/streetpea/Documents/psn-chiaki/token.txt")

	tokenFile, err := os.OpenFile("/tmp/token.txt", os.O_WRONLY|os.O_CREATE, 0666)
    if err != nil {
        fmt.Println("File does not exists or cannot be created")
        os.Exit(1)
    }
	defer tokenFile.Close()
	w2 := bufio.NewWriter(tokenFile)
	fmt.Fprintf(w2, "%v\n", access.AccessToken)
	w2.Flush()

	fmt.Println("Your access token is saved to: /tmp/token.txt")

	fmt.Println("Press Enter to quit")
	reader.ReadString('\n')
}

type AccessKeys struct {
	AccessToken		string	`json:"access_token"`;
	RefreshToken       string  `json:"refresh_token"`;
	ExpiryDate      int  `json:"expires_in"`;
}
