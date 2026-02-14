@echo off
@echo off

REM ==== PATHS (Change if needed) ====
set BRAVE="C:\Program Files\BraveSoftware\Brave-Browser\Application\brave.exe"
set CHROME="C:\Program Files\Google\Chrome\Application\chrome.exe"

REM ==== URLs ====
set EBAY1=https://www.ebay.com/
set EBAY2=https://www.ebay.com/sh/ovw
set EBAY3=https://www.ebay.com/sh/ord
set EBAY4=https://www.ebay.com/sh/lst/active
set EBAY5=https://www.ebay.com/sh/lst/drafts

set CHATGPT=https://chatgpt.com/
set CONVERTCASE=https://convertcase.net/

REM ==== OPEN BRAVE (All Tabs) ====
start "" %BRAVE% ^
%EBAY1% ^
%EBAY2% ^
%EBAY3% ^
%EBAY4% ^
%EBAY5% ^
%CHATGPT% ^
%CONVERTCASE%

REM ==== OPEN CHROME (Only eBay Tabs) ====
start "" %CHROME% ^
%EBAY1% ^
%EBAY2% ^
%EBAY3% ^
%EBAY4% ^
%EBAY5%

exit
