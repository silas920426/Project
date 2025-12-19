document.getElementById("loginBtn").addEventListener("click", async () => {

    const username = document.getElementById("username").value.trim();
    const password = document.getElementById("password").value.trim();

    if (!username || !password) {
        alert("帳號與密碼不能空白！");
        return;
    }

    const res = await fetch("/auth/login", {
        method: "POST",
        headers: { "Content-Type": "application/json" },
        body: JSON.stringify({ username, password })
    });

    const data = await res.json();

    if (res.ok) {
        localStorage.setItem("loggedIn", "true");
        localStorage.setItem("username", username);
        localStorage.setItem("authToken", data.token); // 儲存 token

        alert("登入成功 ! ");
        window.location.href = "/index";
    }else {
            alert(data.message);
        }
});
