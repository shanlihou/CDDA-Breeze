・speech.json
淫魔の通常時のセリフのファイルです。(カンビオンは本体のjson/speech.json)
セリフを追加する場合、

  {
    "type": "speech",
    "speaker": "モンスターのid",
    "sound": "「セリフ」",
    "volume": 音量
  }

という感じでファイルに追加してください。

・snippet.json
お楽しみセリフと裸のプレイヤーへの反応セリフのファイルです。

スニペットid
お楽しみ開始時：モンスターid_hentai_play_start 
お楽しみ中　　：モンスターid_hentai_play_doing
お楽しみ終了時：モンスターid_hentai_play_start 
裸反応セリフ　：モンスターid_hentai_player_is_naked 

セリフの追加方法
追加するモンスターの該当セリフの項目がすでに存在する場合、その項目に付け加えてください。
存在しない場合、ファイル一番上のEOC: "basic_cubi_speech_setup_snippets"のeffectに

  { "math": [ "スニペットid", "=", "1" ] }

を追加し、更に

  {
    "type": "snippet",
    "category": "スニペットid",
    "text": [
      "セリフ1",
      "セリフ2",
      ...
    ]
  }

をファイルに追加してください。

・chat_cubi.json
淫魔との会話のセリフを上書きするファイルです。
会話id
最初の会話 : talk_モンスターid
コンドーム使用時会話 : talk_モンスターid_with_condom
ナマの会話 : talk_モンスターid_without_condom


  {
    "type": "talk_topic",
    "id": "会話id",
    "dynamic_line": [
      "セリフ1",
      "セリフ2",
      ...
    ]
  }

という風に追加してください。
例えばサキュバス・ラクトの初期セリフを設定するには
  {
    "type": "talk_topic",
    "id": "talk_mon_succubi_lactophilia",
    "dynamic_line": [
      "あらあら",
      "うふふ",
      "♡"
    ]
  }
というふうに追加してください。

・モンスターid
カンビオン男: mon_cambion_male
カンビオン女: mon_cambion_female

レッサーサキュバス: mon_lessor_succubi
サキュバス: mon_succubi
サキュバス・サディスト: mon_succubi_sadist
サキュバス・ソムノ: mon_succubi_somnophilia
サキュバス・エクス: mon_succubi_exhibitionism
サキュバス・ラクト: mon_succubi_lactophilia
グレーターサキュバス: mon_greater_succubi

レッサーインキュバス: mon_lessor_incubi
インキュバス: mon_incubi
インキュバス・ステノ: mon_incubi_sthenolagnia
インキュバス・ファルス: mon_incubi_phalloplas
インキュバス・ホプロ: mon_incubi_hoplophilia
グレーターインキュバス: mon_greater_incubi

堕ちた女子学生: mon_corrupted_schoolgirl
堕ちた女教師: mon_corrupted_schoolteacher_female
堕ちた男子学生: mon_corrupted_schoolboy
堕ちた教師: mon_corrupted_schoolteacher_male
