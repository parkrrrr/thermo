
var lastTimeLogged = 0;

function ShowProgramDialog() {
    document.getElementById("programs").style.visibility = "visible";
}

function HideProgramDialog() {
    document.getElementById("programs").style.visibility = "hidden";
}

function DrawGraph(parameters) {
    var canvas = document.getElementById("fullscreen");
    var ctx = canvas.getContext("2d");
    var width = canvas.width;
    var height = canvas.height;
    var pphundred = Math.round(height/16);
    var pphour = pphundred; // set the time axis such that 100 dph is a 45-degree line
    var dispSec = Math.round(3600 * width / pphour);

    ctx.translate(0,height);

    // draw temp lines
    ctx.strokeStyle = "#999999";
    ctx.fillStyle = "#999999";
    ctx.textBaseline = "bottom";
    ctx.textAlign = "right";
    ctx.font = "12pt sans-serif";
    var y = 0;
    var t = 0;

    ctx.beginPath();
    for ( y = 0, t = 0; y < height; y += pphundred, t += 100 ) {
        ctx.moveTo(0,-y); ctx.lineTo(width,-y); 
        ctx.fillText(t.toString()+"   ", width, -y);
    }
    ctx.stroke();

    ctx.scale( width/dispSec, pphundred/100 );
    ctx.translate( -parameters["startTime"], 0);
 
    // draw seg lines
    ctx.strokeStyle = "#999900";
    var key = "";
    ctx.beginPath();
    for ( key in parameters["segments"] ) { 
        var xt = parseInt(key );
        ctx.moveTo(xt,0); ctx.lineTo(xt,-1700); 
    }
    ctx.stroke();

    // draw temp graph
    ctx.strokeStyle = "#333399";
    ctx.lineWidth = ctx.lineWidth * 2;
    var i = 0;
    ctx.beginPath();
    ctx.moveTo( parameters["temps"][0][0], -parameters["temps"][0][1] );
    for (i = 1; i < parameters["temps"].length; ++i ) {
        ctx.lineTo( parameters["temps"][i][0], -parameters["temps"][i][1] );
    }
    ctx.stroke(); 
}

function UpdateGraph() {
    var canvas = document.getElementById("fullscreen");
    var width = parseInt(getComputedStyle(canvas).width);
    var height = parseInt(getComputedStyle(canvas).height); 
    if ( canvas.width != width ) canvas.width = width;
    if ( canvas.height != height ) canvas.height = height;

    var pphundred = height/16;
    var pphour = pphundred; // set the time axis such that 100 dph is a 45-degree line
    var dispSec = Math.round(3600 * width / pphour);

    var request = new XMLHttpRequest;
    request.open( "GET", "/cgi-bin/log.cgi?sec=" + dispSec.toString() );
    request.responseType = "json";
    request.onreadystatechange = function () {
        if(request.readyState === XMLHttpRequest.DONE && request.status === 200) {
            var response = request.response;
            DrawGraph( response);
        }
    }
    request.send();
}

function Update() {
    var request = new XMLHttpRequest;
    request.open( "GET", "/cgi-bin/status.cgi" );
    request.responseType = "json"; 
    request.onreadystatechange = function () {
        if(request.readyState === XMLHttpRequest.DONE && request.status === 200) {
            var response = request.response;
            document.getElementById("pv").innerText = response["pv"];
            document.getElementById("sv").innerText = response["sv"];
            document.getElementById("elt").innerText = response["elapsedTime"];
            document.getElementById("sct").innerText = response["plannedTime"];
            if ( lastTimeLogged != response.lastTime ) {
                lastTimeLogged = response.lastTime;
                UpdateGraph();
            } 
        }
    };
    request.send();
}

function Setup() {
    document.getElementById("select").addEventListener("click", ShowProgramDialog);
    document.getElementById("back").addEventListener("click", HideProgramDialog);
    setInterval(Update,1000);
}

setTimeout( Setup, 1000);
