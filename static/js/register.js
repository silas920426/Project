document.getElementById("registerBtn").addEventListener("click", async () => {

    const username = document.getElementById("username").value.trim();
    const email = document.getElementById("email").value.trim();
    const password = document.getElementById("password").value.trim();

    if (!username || !email || !password) {
        alert("所有欄位必須填寫！");
        return;
    }

    const res = await fetch("/auth/register", {
        method: "POST",
        headers: { "Content-Type": "application/json" },
        body: JSON.stringify({
            username: username,
            email: email,     // ⚠️ 你的後端目前沒有 email 欄位
            password: password
        })
    });

    const data = await res.json();
    alert(data.message);

    if (res.ok) {
        window.location.href = "/login";
    }
});
