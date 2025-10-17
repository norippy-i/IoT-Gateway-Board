[README in English](./README.md)

IoT Gateway Board は、SDカードストレージとLEDステータス表示を備えた多機能 IoT ハブを作りたい方向けのプロジェクトです。

# インストールガイド
最初に、VSCode に PlatformIO をインストールしてください。

1. プロジェクトフォルダをダウンロードし、「Open Project」を使って VSCode で開きます。
2. PlatformIO は platformio.ini ファイルに基づいて必要なライブラリを自動的にインストールします。手動でインストールする必要はありません。
3. デバイスを PC に接続してください。
4. VSCode の左下隅で PlatformIO メニューを開き、Upload を選択します。これでファームウェアがビルドされ、デバイスに書き込まれます。

# プログラムの動作について
サンプルコードでは以下の動作をします。動作の詳細についてはサンプルコードのコメントをご確認ください

***タクトスイッチ*** : 押されると、シリアル通信でメッセージが表示されます。

***SD カード*** : SD カードが挿入されている場合、タクトスイッチを押すとhello という名前のテキストファイルがSD　カードに書き込まれます。

***フルカラー LED (WS2812B)*** : 常時点灯。時間と共に色が変化していきます。

***スライドスイッチ*** :

	•	一方に切り替える → シリアルに mode 1 が表示される
 
	•	もう一方に切り替える → シリアルに mode 2 が表示される
 
	•	中間にある場合 → シリアルに unknown が表示される

# 筐体について

IoT Gateway Board専用の筐体をデザインしました。

スナップフィットによりネジを使うことなく組み立てができるようになっています。

この筐体は3Dプリンタでの製造ができることも確認できていますので、家にある3Dプリンタで製造してみて下さい。

***Note*** : この筐体は以下の環境で問題なく製造ができることを確認してます。

	•	3Dプリンタ: Bmamulab X1 Carbon
	•	ノズル: 0.4mm
	•	材料: PLA


# サンプルコードについて
このリポジトリのサンプルコードは以下のライブラリを使用しています:

- [FastLED](https://github.com/FastLED/FastLED)（ライセンス: MIT）
-	SPI（Arduino 標準ライブラリ、ライセンス: LGPL-2.1）
-	SD（Arduino 標準ライブラリ、ライセンス: LGPL-2.1）

# Switch2を確実に買うためのIoT　Gateway Boardを活用したサンプル
このIoT Gateway boardをフル活用したシステムを構築しました

[ゲリラ入荷お知らせシステム -絶対にSwitch2買う!-](https://protopedia.net/prototype/7610)

こちらのIoT Gateway部分のサンプルコードと、ESP-NOWでデータを受け取るデバイスのサンプルコードを用意しました。

ESP-NOWでデータを受け取るデバイスは[M5ATOM Lite](https://www.switch-science.com/products/6262?srsltid=AfmBOop3nCKR_6CEb2lsimvxAGZrY1MQYfr4301_rZzS-clmA1ZFM4YU)を使っております。
自作のLEDボードなどを繋いでいるので、このままの状態で動かすのはハードルが高いので、あくまでサンプルとしてご活用ください。

