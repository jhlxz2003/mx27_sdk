#ifndef PROTO_H_
#define PROTO_H_

/* caller->callee */
#define  CallReqMsg      ((int)500)
#define  LvWrdCfmMsg    ((int)506)

/* callee->caller */
#define  CallReqAck      ((int)501)
#define  RingMsg         ((int)502)
#define  AcceptMsg       ((int)503)
#define  OpenDoorMsg     ((int)504)
#define  AskLvWrdMsg     ((int)505)
#define  LvWrdStartMsg   ((int)507)
#define  BusyMsg         ((int)509)

/* caller<->callee */
#define  HupMsg          ((int)508)
#define  SsTmeoMsg       ((int)510)

#define  MuteStartMsg    ((int)511)
#define  MuteEndMsg      ((int)512)

/* new add on 2009.10.28 */
/* callee->caller */
#define  DenyMsg        ((int)513)
#define  DenyLmMsg      ((int)514)

//////////////////////////////
/* slave shift call after accepted */
#define  AccSftMsg       ((int)516)
/* slave accepted */
/* master host -> caller */
#define  AccSlvMsg       ((int)515)

#define  CameraOn        ((int)517)
#define  CameraOff        ((int)518)

#define  CallReqNV        ((int)519)
#define  AcceptNV        ((int)520)
/* master to slave */
#define  RingNV         ((int)521)
#define  AccSlvNV       ((int)522)

#define  AcceptAck   ((int)523)
#define  AccSlvAck   ((int)524)
////////////////////////////////

#define OfflineAlarm    ((int)301)
#define TriggerAlarm    ((int)302)
#define OLNoticeMsg     ((int)303)
#define AlertEndMsg     ((int)304)

#define ElvResrv          ((int)101010)

#define  LOG_MSG     ((int)500000)
#define  LOG_RET     ((int)500001)
#define  LOGOUT_MSG  ((int)500002)
#define  LOGOUT_RET  ((int)500003)
#define  HB_MSG      ((int)500004)
#define  HB_RET      ((int)500005)

#define  SLV_LOGON_TMEO    10
#define  SLV_HB_TMEO      30
#define  REQ_TMEO      10

#define  KEEP_ALIVE_IDLE   3
#define  PROBE_ITVL     1
#define  PROBE_CNT      1

/* remote control */
#define  ArmZone      ((int)10101)
#define  DisarmZone   ((int)10102)

#define  AllArm          ((int)10103)
#define  AllDisarm     ((int)10104)

#define  ZoneStReq    ((int)10105)
#define  ZoneStAck    ((int)10106)

#define  SceneCall    ((int)10201)
#define  AllLtOnOff    ((int)10301)
#define  LtOnOff        ((int)10302)

#define  AllCtOnOff     ((int)10402)
#define  CtOnOff        ((int)10401)

#define  AllAcOnOff      ((int)10502)
#define  AcOnOff         ((int)10501)

#define  AllWdOnOff       ((int)10602)
#define  WdOnOff         ((int)10601)

#define  AllFhOnOff         ((int)10702)
#define  FhOnOff            ((int)10701)

#define  MusicOnOff         ((int)10801)

#endif

