document.querySelectorAll("pre").forEach((block) => {
  const code = block.querySelector("code");
  if (!code) {
    return;
  }

  block.classList.add("has-copy");

  const button = document.createElement("button");
  button.className = "copy-button";
  button.type = "button";
  button.textContent = "Copy";
  button.setAttribute("aria-label", "Copy code block");

  button.addEventListener("click", async () => {
    try {
      await navigator.clipboard.writeText(code.innerText);
      button.textContent = "Copied";
    } catch (error) {
      button.textContent = "Select";
    }

    window.setTimeout(() => {
      button.textContent = "Copy";
    }, 1400);
  });

  block.appendChild(button);
});
