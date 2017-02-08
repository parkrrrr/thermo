
function ShowProgramDialog() {
    document.getElementById("programs").style.visibility = "visible";
}

function HideProgramDialog() {
    document.getElementById("programs").style.visibility = "hidden";
}


function Setup() {
    document.getElementById("select").addEventListener("click", ShowProgramDialog);
    document.getElementById("back").addEventListener("click", HideProgramDialog);
}

setTimeout( Setup, 1000);
