package main

import (
	"fmt"
	"github.com/tarm/serial"
	"log"
	"time"
)

func main() {
	serialConfig := serial.Config{
		Name: "/dev/cu.usbmodemfa131",
		Baud: 115200,
		// ReadTimeout:    options.SerialTimeout,
	}

	var sp *serial.Port

	if serialPort, err := serial.OpenPort(&serialConfig); err != nil {
		fmt.Errorf("serial.OpenPort: %v", err)
		panic(err)
	} else {
		sp = serialPort
	}

	time.Sleep(5 * time.Second)

	tallies := "123456789ABCDEF"
	states := "01234"

	for {
		for _, t := range []byte(tallies) {
			for _, s := range []byte(states) {
				msg := fmt.Sprintf("Q%c%cW", t, s)
				fmt.Printf("T: %c S: %c Sending: %s", t, s, msg)
				n, err := sp.Write([]byte(msg))
				fmt.Printf(" Wrote %d bytes\n", n)
				if err != nil {
					log.Fatal(err)
				}
				time.Sleep(500 * time.Millisecond)
			}
		}
	}
}
