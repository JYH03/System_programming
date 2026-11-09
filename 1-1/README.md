# Assignment 1-1: 명령행 옵션 분석

`getopt()`로 `-a`, `-b`, 문자열 인자를 받는 `-c` 옵션을 분석하고 나머지 일반 인자를 출력한다. `-a`와 `-b`는 입력 횟수를 세며, 알 수 없는 옵션은 무시한다.

## 빌드와 실행

Linux 환경에서 GCC를 사용한다. 현재 폴더에 Makefile이 없어 직접 컴파일한다.

```bash
gcc -Wall -o kw2022202078_opt kw2022202078_opt.c
./kw2022202078_opt -ab -c hello example
```

`optarg`로 `-c`의 값을 읽고 `optind` 이후의 인자를 출력한다.
