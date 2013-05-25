all:
	echo "You probably accidentally told Eclipse to build.  Stopping."

once:
	CXX=g++-4.7 CXXFLAGS='-std=gnu++0x -O2 -Wall' LIBS='-lz -pthread -lpcrecpp -lcrypto' ekam -j6

once-clang:
	CXX=clang++ CXXFLAGS='-fno-caret-diagnostics -std=c++11 -O2 -Wall' LIBS='-lz -pthread -lpcrecpp -lcrypto' ekam -j6

continuous:
	CXX=g++-4.7 CXXFLAGS='-std=gnu++0x -g -Wall' LIBS='-lz -pthread -lpcrecpp -lcrypto' ekam -j6 -c -n :51315

continuous-clang:
	CXX=clang++ CXXFLAGS='-fno-caret-diagnostics -std=c++11 -g -Wall' LIBS='-lz -pthread -lpcrecpp -lcrypto' ekam -j6 -c -n :51315

clean:
	rm -rf bin lib tmp

