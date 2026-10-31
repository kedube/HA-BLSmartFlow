// Drifting particles behind every page; the canvas takes its blue from style.css.
(function () {
  const canvas = document.getElementById('particleCanvas');
  if (!canvas) return;
  const ctx = canvas.getContext('2d');
  const numberOfParticles = 100;
  let particlesArray = [];

  class Particle {
    constructor(x, y, directionX, directionY, size) {
      this.x = x;
      this.y = y;
      this.directionX = directionX;
      this.directionY = directionY;
      this.size = size;
    }
    draw() {
      ctx.beginPath();
      ctx.arc(this.x, this.y, this.size, 0, Math.PI * 2);
      ctx.fillStyle = 'rgba(255, 255, 255, 0.8)';
      ctx.fill();
    }
    update() {
      if (this.x + this.size > canvas.width || this.x - this.size < 0) this.directionX = -this.directionX;
      if (this.y + this.size > canvas.height || this.y - this.size < 0) this.directionY = -this.directionY;
      this.x += this.directionX;
      this.y += this.directionY;
      this.draw();
    }
  }

  function init() {
    canvas.width = window.innerWidth;
    canvas.height = window.innerHeight;
    particlesArray = [];
    for (let i = 0; i < numberOfParticles; i++) {
      const size = Math.random() * 5 + 1;
      const x = Math.random() * (canvas.width - size * 2) + size;
      const y = Math.random() * (canvas.height - size * 2) + size;
      particlesArray.push(new Particle(x, y, Math.random() * 0.5 - 0.25, Math.random() * 0.5 - 0.25, size));
    }
  }

  function connect() {
    const threshold = (canvas.width / 7) * (canvas.height / 7);
    for (let a = 0; a < particlesArray.length; a++) {
      for (let b = a; b < particlesArray.length; b++) {
        const distance = (particlesArray[a].x - particlesArray[b].x) ** 2 + (particlesArray[a].y - particlesArray[b].y) ** 2;
        if (distance < threshold) {
          ctx.strokeStyle = `rgba(255, 255, 255, ${1 - distance / 20000})`;
          ctx.lineWidth = 1;
          ctx.beginPath();
          ctx.moveTo(particlesArray[a].x, particlesArray[a].y);
          ctx.lineTo(particlesArray[b].x, particlesArray[b].y);
          ctx.stroke();
        }
      }
    }
  }

  function animate() {
    ctx.clearRect(0, 0, canvas.width, canvas.height);
    for (const particle of particlesArray) particle.update();
    connect();
    requestAnimationFrame(animate);
  }

  window.addEventListener('resize', init);
  init();
  animate();
})();
