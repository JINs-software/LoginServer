### JNetLibrary 코어 라이브러리 기반 로그인 서버

https://github.com/JINs-software/LoginServer_JNet

<p align="center">
  <img src="[https://velog.velcdn.com/images/jinh2352/post/5b35ce1b-7722-4257-b6f8-3c1dbd7288e1/image.png](https://github.com/user-attachments/assets/79177fdc-c687-4437-a7a5-cb04c07e2cec)" width="600">
</p>

> 현재 프로젝트는 구 버전 라이브러리인 CLanLibrary(https://github.com/JINs-software/CLanLibrary) 기반이며, 성능 및 기능 테스트는 현 프로젝트에서 진행)

### \[로그인 서버(with 채팅 서버) 성능/오류 테스트\]
* 7일간의 로그인-채팅 서버 연동 테스트
  ![image](https://github.com/user-attachments/assets/94c10b75-1f2e-4191-8637-5edf9650285a)

#### 테스트 조건
* 채팅 서버 Accept TPS: 200 이상
* Message Update TPS: 5000 이상
* Action Delay Avr: 60ms 이하
* 더미 프로그램(프로카데미 제공)
  * 더미 그룹: 5000명 재접속 모드
 
#### 테스트 결과 => 통과!
* 더미 그룹 실행 중
  ![image](https://github.com/user-attachments/assets/4e28d144-f0bb-4982-8264-97b957b9978d)
  ![image](https://github.com/user-attachments/assets/19a9fd0c-dfba-40dc-befc-4ba9b5566663)
  ![image](https://github.com/user-attachments/assets/c849befa-fc64-4f3e-af8a-eb4296877fb1)
  * 초 당 인증 처리 수: 253.90
  * 초 당 채팅 메시지 처리 수: 8081.65
  * 더미 그룹 딜레이 평균: 47ms

* 추가 캡쳐본
  * 더미 그룹 중지 전, 로그인 서버-채팅 서버 콘솔
    ![image](https://github.com/user-attachments/assets/dc93d2dc-e271-47f3-8bf6-055319aa5af7)
  * 더미 그룹 중지 후,       "  "
    ![image](https://github.com/user-attachments/assets/3eecab09-36e6-472f-b3e9-2d9c0c7280d3)

