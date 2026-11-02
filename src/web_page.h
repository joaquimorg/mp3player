#pragma once
// Web pages served from flash (PROGMEM). Portuguese (Portugal) UI.
// Page text is inserted with textContent only (never innerHTML), so file names
// cannot inject markup.
#include <Arduino.h>

static const char kMainPage[] PROGMEM = R"HTML(<!DOCTYPE html>
<html lang="pt-PT"><head><meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>Relógio MP3</title>
<style>
*{box-sizing:border-box}
body{font-family:system-ui,sans-serif;margin:0;background:#101418;color:#e8edf2}
main{max-width:34rem;margin:0 auto;padding:1rem}
h1{font-size:1.3rem;margin:.2rem 0 .2rem}
.free{color:#9fb0c0;margin:0 0 1rem;font-size:.95rem}
.slot{background:#1b222a;border-radius:.7rem;padding:.8rem;margin-bottom:.8rem}
.top{display:flex;justify-content:space-between;gap:.5rem;align-items:baseline}
.nm{font-weight:600;overflow-wrap:anywhere}
.sz{color:#9fb0c0;font-size:.9rem;white-space:nowrap}
.row{display:flex;gap:.5rem;margin-top:.6rem;flex-wrap:wrap}
input[type=file]{flex:1 1 12rem;min-width:0;color:#cfd8e0;font-size:.9rem}
button{font-size:1rem;padding:.55rem 1rem;border:0;border-radius:.5rem;cursor:pointer;min-height:2.6rem}
.up{background:#2f7dd1;color:#fff}.del{background:#5a2a2a;color:#fff}
button:disabled{opacity:.45;cursor:default}
progress{width:100%;height:.7rem;margin-top:.6rem;display:none}
.msg{margin-top:.5rem;font-size:.9rem;min-height:1.1rem}
.err{color:#ff8a80}.ok{color:#8fe3a1}
</style></head><body><main>
<h1>Relógio MP3</h1>
<p class="free" id="free">A carregar…</p>
<div id="slots"></div>
</main>
<script>
var MAX=3*1024*1024,FREE=0;
function fmt(n){return n>=1048576?(n/1048576).toFixed(2)+' MB':n>=1024?(n/1024).toFixed(0)+' KB':n+' B';}
function el(t,c,x){var e=document.createElement(t);if(c)e.className=c;if(x!==undefined)e.textContent=x;return e;}
function load(keepMsg){
  fetch('/api/slots').then(function(r){return r.json();}).then(function(d){
    FREE=d.free;document.getElementById('free').textContent='Espaço livre: '+fmt(d.free)+' · máximo por ficheiro: 3 MB';
    var box=document.getElementById('slots');
    var old={};[].forEach.call(box.querySelectorAll('.msg'),function(m){old[m.dataset.n]=[m.className,m.textContent];});
    box.textContent='';
    d.slots.forEach(function(s){box.appendChild(card(s,keepMsg?old[s.n]:null));});
  }).catch(function(){document.getElementById('free').textContent='Sem ligação ao relógio.';});
}
function card(s,prev){
  var c=el('div','slot'),t=el('div','top');
  t.appendChild(el('span','nm','Slot '+s.n+' – '+(s.size?s.name:'(vazio)')));
  t.appendChild(el('span','sz',s.size?fmt(s.size):''));
  c.appendChild(t);
  var row=el('div','row'),f=el('input');f.type='file';f.accept='.mp3,audio/mpeg';
  var u=el('button','up','Enviar'),x=el('button','del','Apagar');x.disabled=!s.size;
  row.appendChild(f);row.appendChild(u);row.appendChild(x);c.appendChild(row);
  var p=el('progress');p.max=100;p.value=0;c.appendChild(p);
  var m=el('div','msg');m.dataset.n=s.n;if(prev){m.className=prev[0];m.textContent=prev[1];}c.appendChild(m);
  function say(k,txt){m.className='msg '+k;m.textContent=txt;}
  u.onclick=function(){
    var file=f.files[0];
    if(!file){say('err','Escolhe primeiro um ficheiro MP3.');return;}
    if(file.size>MAX){say('err','Ficheiro demasiado grande (máximo 3 MB).');return;}
    if(file.size>FREE){say('err',s.size?'Sem espaço: apaga primeiro este slot e volta a enviar.':'Sem espaço livre no relógio. Apaga outro slot e tenta de novo.');return;}
    if(s.size&&!confirm('Substituir o conteúdo do slot '+s.n+'?'))return;
    var fd=new FormData();fd.append('file',file,file.name);
    var xhr=new XMLHttpRequest();xhr.open('POST','/api/slot/'+s.n);
    u.disabled=x.disabled=true;p.style.display='block';p.value=0;say('','A enviar…');
    xhr.upload.onprogress=function(e){if(e.lengthComputable)p.value=100*e.loaded/e.total;};
    xhr.onload=function(){
      var r={};try{r=JSON.parse(xhr.responseText);}catch(e){}
      if(xhr.status==200){load(false);}
      else{u.disabled=false;x.disabled=!s.size;p.style.display='none';say('err',r.error||('Erro '+xhr.status));}
    };
    xhr.onerror=function(){u.disabled=false;x.disabled=!s.size;p.style.display='none';say('err','Falha na ligação durante o envio.');};
    xhr.send(fd);
  };
  x.onclick=function(){
    if(!confirm('Apagar o slot '+s.n+'?'))return;
    x.disabled=u.disabled=true;
    fetch('/api/slot/'+s.n,{method:'DELETE'}).then(function(r){return r.json().then(function(j){return [r.ok,j];});})
    .then(function(a){if(a[0])load(false);else{u.disabled=false;say('err',a[1].error||'Erro ao apagar.');}})
    .catch(function(){u.disabled=false;say('err','Falha na ligação.');});
  };
  return c;
}
load(false);
</script></body></html>)HTML";

static const char kSetupPage[] PROGMEM = R"HTML(<!DOCTYPE html>
<html lang="pt-PT"><head><meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>Configurar Wi-Fi</title>
<style>
body{font-family:system-ui,sans-serif;margin:0;background:#101418;color:#e8edf2}
main{max-width:26rem;margin:0 auto;padding:1rem}
label{display:block;margin:.8rem 0 .3rem}
input{width:100%;font-size:1rem;padding:.6rem;border-radius:.4rem;border:1px solid #445;background:#1b222a;color:#fff}
button{margin-top:1.2rem;width:100%;font-size:1.05rem;padding:.7rem;border:0;border-radius:.5rem;background:#2f7dd1;color:#fff}
</style></head><body><main>
<h1>Configurar Wi-Fi</h1>
<p>Indica a rede Wi-Fi (2,4 GHz) a que o relógio se deve ligar. Depois de guardar, o relógio reinicia.</p>
<form method="POST" action="/wifi" accept-charset="utf-8">
<label for="s">Nome da rede (SSID)</label>
<input id="s" name="ssid" required maxlength="32" autocapitalize="none" autocomplete="off">
<label for="p">Palavra-passe</label>
<input id="p" name="pass" type="password" maxlength="63" autocomplete="off">
<button type="submit">Guardar e reiniciar</button>
</form></main></body></html>)HTML";

static const char kSetupSavedPage[] PROGMEM = R"HTML(<!DOCTYPE html>
<html lang="pt-PT"><head><meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>Guardado</title></head><body style="font-family:system-ui,sans-serif;background:#101418;color:#e8edf2;padding:1rem">
<h1>Guardado</h1>
<p>O relógio vai reiniciar e ligar-se à rede indicada. Volta a ligar o telemóvel ao teu Wi-Fi normal e abre o endereço que aparece no ecrã do relógio.</p>
</body></html>)HTML";
