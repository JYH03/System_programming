# Assignment 1-3: FTP 명령 변환과 처리

클라이언트가 사용자 명령을 FTP 형식으로 변환하고, 서버가 표준 입력으로 받은 명령을 처리하는 프로그램이다. 이 단계에서는 소켓 대신 표준 입출력을 사용한다.

## 주요 내용

- `ls`, `dir`, `pwd`, `cd`, `mkdir`, `delete`, `rmdir`, `rename`, `get`, `put`, `quit`을 FTP 명령으로 변환한다.
- 서버는 목록 조회, 경로 변경, 파일·디렉터리 관리와 파일 복사 등을 처리한다.
- `get`과 `put`은 로컬 `server_root`와 `client_root` 사이에서 파일을 복사하는 방식이다.

## 빌드와 실행

```bash
make
./cli ls -al | ./srv
```

파일 복사를 시험하려면 실행 디렉터리에 `client_root`와 `server_root`를 먼저 준비해야 한다. 빌드 결과는 `make clean`으로 정리할 수 있다.
