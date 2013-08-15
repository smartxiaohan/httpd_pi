httpd_pi: httpd_pi.cpp
	g++ -Wall -O2 httpd_pi.cpp -o httpd_pi

clean:
	rm -f httpd_pi
