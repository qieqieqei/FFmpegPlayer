# 闃舵 5 鎵ц璁″垝锛氱绾?鍛堢幇鑱岃矗澶栫Щ锛圦ueue 璺敱 鈫?VideoPresenter 鈫?Run 缂栨帓鏀舵暃锛?
> 璧风偣锛歚core/Player.cpp` **3061 琛?/ 88 涓柟娉?*锛坄Player.h` 533 琛岋級锛宍PlaybackSession.cpp` 1626 琛屻€?> 鐩爣锛氱户缁妸**绠＄嚎/鍛堢幇**鑱岃矗浠?Player 绉诲嚭锛岃 `Run()` 鏀舵暃涓?缂栨帓楠ㄦ灦"銆?> 纭害鏉燂紙娌跨敤 `refactor-rules.md`锛夛細鍏堝璁″悗鏀癸紱绂佹涓€娆℃€уぇ閲嶆瀯锛涙瘡瀛愭 `鏀?鈫?MSBuild Debug|x64 鈫?Release|x64 鈫?鍥炲綊 鈫?鐙珛鎻愪氦`锛涗笉鏀硅涓猴紱涓嶉『鎵嬩慨 Bug锛涗笉纭畾涓嶅垹锛涚姝㈡満姊版媶鏂囦欢銆?
## 1. 瀹炴祴鐜扮姸锛坄phase5_scan.js`锛屾寜鑺辨嫭鍙烽厤瀵圭粺璁★級

| 鏂规硶 | 琛屾暟 | 褰掔被 |
|---|---|---|
| `Run` | **645** | 娓叉煋涓诲惊鐜紙鍙栧抚/鍚屾/娓叉煋/OSD/缁熻 + switch/reconnect/buffer-gate 缂栨帓锛墊
| `UpdateStatistics` | 198 | 缁熻閲囨牱 |
| `Init` | 117 | 瑁呴厤 |
| `StartHLS` / `StartPushing` / `StartRecording` | 117 / 96 / 71 | 杈撳嚭閾?|
| `EnsureOutEncoders` / `FeedOutputVideo` / `ToYuv420p` / `FeedOutputAudio` / `FlushOutEncoders` / `StopAllOutputs` / `DispatchVideoPacket` / `ReleaseOutEncoders` | 112 / 73 / 71 / 58 / 63 / 52 / 50 / 28 | 杈撳嚭閾撅紙鈮?*800 琛?*锛岄樁娈?7 鏁寸皣澶栫Щ锛墊
| `ExpandPlaylistWithSiblings` | 106 | features/playlist |
| `GetSwsForFrame` | 50 | **鍛堢幇**锛圫DL/sws/RGB锛墊
| `PushVideoPacket` / `PushAudioPacket` | 34 / 31 | **闃熷垪璺敱** |
| 鍚?`Is*/Get*Queue*` | 4鈥? 脳6 | 闃熷垪璺敱 |
| 20+ 涓?4 琛岃闂櫒 | 鈮?0 | Facade API锛?*淇濈暀**锛墊

缁撹锛氶樁娈?5 鐨勫彲鎼璞?= **闃熷垪璺敱锛堚増110 琛岋級+ 鍛堢幇绨囷紙鈮?50鈥?50 琛岋級+ `Run()` 鍐呯殑缂栨帓鍧楋紙鈮?50 琛岋級**銆傝緭鍑洪摼涓庣粺璁＄暀鍒伴樁娈?7 / 闃舵 6銆?
## 2. 瀛愭

| 姝?| 鍐呭 | 椋庨櫓 | 楠岃瘉 |
|---|---|---|---|
| **5.1** | 闃熷垪璺敱澶栫Щ锛歚PushVideoPacket/PushAudioPacket/PopVideoPacket/PopAudioPacket/IsVideoQueueInterrupted/IsAudioQueueInterrupted/GetVideoQueueSize/GetAudioQueueSize/GetVideoQueueCapacity` + 涓㈠寘閿氱偣 `lastVideoDropped/lastAudioDropped` 鈫?**`PlaybackSession`**锛圖emux/瑙ｇ爜绾跨▼鐨勫涓伙級锛汸layer 渚т粎 `session->Get*QueueSize()` 渚涚粺璁?| 浣?| 缂栬瘧+鍥炲綊锛涜皟鐢ㄧ偣鏍稿 |
| **5.2** | 鍛堢幇绨?鈫?鏂?`output/video/VideoPresenter`锛歋DL 璧勬簮锛坄window/renderer/texture/rgbTexture`锛? `swsCtx/swsSrc*` + `rgbData/rgbLinesize` + `lastFrame` + 鏂规硶 `GetSwsForFrame/TakeScreenshot/GetWindow/GetRGBData/GetRGBLinesize/GetRGBTexture` | 涓?| 缂栬瘧+鍥炲綊锛圙UI 璺緞缁?`--record` 璺戦€氾級|
| **5.3** | `Run()` 鍐?鍙栧抚 鈫?闊宠棰戝悓姝?鈫?涓㈠抚 鈫?娓叉煋 鈫?缁熻/鏃堕棿"娈?鈫?`VideoPresenter` 鐨勫憟鐜版柟娉曪紙Run 鍙繚鐣欏惊鐜笌鐘舵€佹満锛?| 涓珮 | 缂栬瘧+鍥炲綊锛堟椂闀?涓㈠抚璁℃暟涓€鑷达級|
| **5.4** | `Run()` 鍐?switch/reconnect 缂栨帓锛堚増250 琛岋級鈫?`PlaybackSession::HandleSwitchRequest(PlayerState&, bool& quit)` / `HandleReconnect(...)` | 涓?| 缂栬瘧+鍥炲綊锛堟湰鍦版棤鏂綉璺緞锛岄潬浠ｇ爜绛変环鎬?+ 鍥炲綊鏃犻€€鍖栵級|
| **5.5** | 闃舵鎻愪氦 + `docs/architecture/phase5-report.md` | 鈥?| Debug+Release + 鍏ㄥ洖褰?|

> 闃舵 5 缁撴潫鍚?`Player.cpp` 棰勬湡 **鈮?400鈥?600 琛?*锛沗Run()` 棰勮 645 鈫?鈮?00鈥?50 琛屻€傚墿浣欏ぇ澶达紙杈撳嚭閾?鈮?00銆佺粺璁?198銆乣Init` 117锛夊垎鍒睘闃舵 7 / 6銆?
## 3. 璁捐瑕佺偣锛堥伩鍏嶅惊鐜緷璧栵級

- **渚濊禆鏂瑰悜**锛坄dependency.md` 6 灞傦級锛歚core` 鈫?`output` 鍏佽锛沗output` **涓嶅緱** 鈫?`core`锛堥樁娈?8 鎷嗗弽鍚戯級銆?  鍥犳 `VideoPresenter` 鍙帴**鏁版嵁**锛屼笉鎺?`Player*`锛氬憟鐜版墍闇€鐨?pts/甯?鐘舵€侀€氳繃鍙傛暟鎴?`RenderContext` 蹇収浼犲叆銆?- **`PlaybackSession` 缁х画鎸?`MediaContext&` + `Player&`**锛堥樁娈?4 瀹氭锛夛紝闃熷垪璺敱绉诲叆鍚庝粛鏄?`media.` 璁块棶銆?- 鏂板绫诲彧鍋?*鐪熷疄澶氳皟鐢ㄧ偣**鐨勬娊鍙栵紱涓嶅缓绌哄３鎺ュ彛銆?
## 4. 鍋滄骞舵姤鍛婄殑鏉′欢锛堟部鐢級

缂栬瘧閿欒鏃犳硶瀹氫綅 / 姝婚攣 / join 鍗℃ / double-free / 闀跨ǔ鎭跺寲 / 琛屼负涓嶅彲鍒ゅ畾 鈫?**绔嬪嵆鍋滄骞舵姤鍛?*銆?
## 5. 鍥炲綊鍙ｅ緞锛堟部鐢ㄩ樁娈?4锛?
`x64\Release\FFmpeg_text_claw.exe --record <sample> --log-file x.log`锛坄--record` 缃?autoQuitOnEof锛夛紝
鏍蜂緥 `124662f108eca04d9189d0efae3829c7.mp4`(12.833s) / `21e1626495c5d9174868eebab99e437c.mp4`(22.655s) / `a4c277.mp4`(141.8s)锛?鍒ゅ畾锛歟xit 0 + FLV 鏃堕暱閫愪綅涓€鑷?+ 鏃?ERROR/WARN銆傛瀯寤?`Debug|x64` 涓?`Release|x64` 鍧?0 error銆?
## 6. 鎵ц璁板綍锛堟粴鍔ㄦ洿鏂帮級

### 5.1 闃熷垪璺敱澶栫Щ 鈥斺€?瀹屾垚锛堟彁浜?`寰呭～`锛?
**鎼Щ娓呭崟**锛? 鏂规硶 + 2 閿氱偣锛宍Player` 鈫?`PlaybackSession`锛夛細

- 鏂规硶锛歚PushVideoPacket` / `PushAudioPacket` / `PopVideoPacket` / `PopAudioPacket` / `IsVideoQueueInterrupted` / `IsAudioQueueInterrupted` / `GetVideoQueueSize` / `GetAudioQueueSize` / `GetVideoQueueCapacity`
- 鎴愬憳锛歚lastVideoDropped` / `lastAudioDropped`
- `PlaybackSession` 鍐呬竴寰?`media.xxx`锛堝紩鐢紝闈炴寚閽堬級锛涗涪鍖呯粺璁＄粡 `owner.networkStatistics`锛坄PlaybackSession` 鏄?`Player` 鐨?friend锛岄樁娈?4 宸插缓绔嬶級
- `PlaybackSession.cpp` 鍐呭師 `owner.Push/Pop*` / `owner.Is*Interrupted` 璋冪敤鏀瑰洖鐩存帴璋冪敤锛堟棤杞彂灞傦級
- `Player` 渚т粎 `Player::UpdateStatistics` 鏈?3 涓皟鐢ㄧ偣锛?653 闄勮繎 `GetVideoQueueSize()脳3 / GetAudioQueueSize()脳1 / GetVideoQueueCapacity()脳1`锛夆啋 鏀瑰墠缂€ `session->`

**瀹炴祴 diff**锛歚Player.h -32`锛堝惈娈垫敞閲婏級銆乣Player.cpp -144`銆乣PlaybackSession.h +30`銆乣PlaybackSession.cpp +149`锛? 涓畾涔夊叏閮?ASCII 娉ㄩ噴锛夈€?
**鏈姩锛堟湁鎰忥級**锛歚MAX_VIDEO_PACKETS=120 / MAX_AUDIO_PACKETS=60 / MAX_VIDEO_FRAMES=12` 浠嶅湪 `core/Player.h:84-88` 鏂囦欢浣滅敤鍩燂紱鍏朵腑 `MAX_VIDEO_FRAMES` 鏃╁凡琚?`PlaybackSession.cpp` 浣跨敤锛堢粡 `Player.h` 浼犻€掑彲瑙侊級銆傞槦鍒楀閲忓父閲忓綊 `pipeline/` 鐨勬竻鐞嗗睘闃舵 8 鏀跺彛锛岄伩鍏嶆湰瀛愭鎵╁ぇ鑼冨洿銆?
**楠岃瘉**锛歚MSBuild Debug|x64` = 0 error / 32 warning锛堜笌 4.4 鍩虹嚎涓€鑷达紝鏃犳柊澧烇級锛沗Release|x64` = 0 error / 32 warning锛涗笁鏍蜂緥 `--record` 鍏ㄩ儴 exit 0锛孎LV 鏃堕暱 **12.833 / 22.655 / 141.8 s**锛堜笌 4.4 閫愪綅涓€鑷达級锛屾棩蹇?ERROR/WARN = 0锛屽熬琛?`Threads Stopped 鈫?State : Stopped 鈫?All outputs stopped 鈫?Closed 脳2`锛堜袱鏉?Closed 涓烘棦鏈?`Close()` 鏃犲箓绛夊畧鍗墍鑷达紝闈炴湰瀛愭寮曞叆锛夈€?