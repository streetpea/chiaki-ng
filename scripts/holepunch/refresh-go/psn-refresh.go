package main

import (
	"encoding/json"
	"net/http"
	"net/url"
	"strings"
	"strconv"
	"bufio"
	"fmt"
	"log"
	"os"
)

func main(){

	client := &http.Client{}
	data :=url.Values{}
	data.Set("grant_type", "refresh_token")
	data.Set("refresh_token", "")
	data.Set("scope", "psn:clientapp referenceDataService:countryConfig.read pushNotification:webSocket.desktop.connect sessionManager:remotePlaySession.system.update")
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
	fmt.Fprintf(w2, "%v", access.AccessToken)
	w2.Flush()

	fmt.Println("Your access token is saved to: /tmp/token.txt")
}

type AccessKeys struct {
	AccessToken		string	`json:"access_token"`;
	RefreshToken       string  `json:"refresh_token"`;
	ExpiryDate      int  `json:"expires_in"`;
}

